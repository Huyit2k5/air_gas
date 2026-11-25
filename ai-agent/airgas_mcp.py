"""MCP server cho trạm cảnh báo gas (ESP32 + MQ-2 + MQ-135).

Đọc số liệu và điều khiển thiết bị qua REST API của Home Assistant. Các thực thể
do Node-RED khai báo bằng MQTT discovery; server tìm chúng theo tên hiển thị
nên không phụ thuộc vào entity_id mà HA tự sinh.

Chạy bằng stdio: python airgas_mcp.py  (agent.py tự khởi động nó).
"""
from __future__ import annotations

import os
import statistics
import sys
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Literal

import logging

import httpx
from dotenv import load_dotenv
from mcp.server.mcpserver import MCPServer
from mcp.server.mcpserver.exceptions import ToolError
from mcp.types import ToolAnnotations

load_dotenv(Path(__file__).with_name(".env"))
HA_URL = os.environ.get("HA_URL", "http://localhost:8123").rstrip("/")
HA_TOKEN = os.environ.get("HA_TOKEN", "")
logging.getLogger("httpx").setLevel(logging.WARNING)  # bỏ log mỗi request
VN_TZ = timezone(timedelta(hours=7))  # Việt Nam không đổi giờ mùa hè

# Ngưỡng trong firmware (Kconfig GAS_THRESHOLD_PPM, AQ_THRESHOLD_PPM)
GAS_THRESHOLD_PPM = 1000
CO2_THRESHOLD_PPM = 1500

# khóa -> (domain, tên thực thể trong HA). friendly_name = "<tên thiết bị> <tên thực thể>"
ENTITIES: dict[str, tuple[str, str]] = {
    "gas": ("sensor", "Nồng độ gas"),
    "co2": ("sensor", "CO2 tương đương"),
    "level": ("sensor", "Mức cảnh báo"),
    "gas_alarm": ("binary_sensor", "Rò rỉ gas"),
    "valve": ("sensor", "Van gas"),
    "fan": ("sensor", "Quạt hút"),
    "buzzer": ("sensor", "Còi báo động"),
    "firmware": ("sensor", "Phiên bản firmware"),
    "wifi_rssi": ("sensor", "Sóng Wi-Fi"),
}
BUTTONS: dict[str, str] = {
    "open_valve": "Xác nhận an toàn – Mở van",
    "close_valve": "Khoá van",
    "test_fan": "Thử quạt 10 giây",
    "test_buzzer": "Thử còi 3 giây",
    "recalibrate": "Hiệu chuẩn cảm biến",
}
LEVEL_TEXT = {
    "BINH_THUONG": "bình thường",
    "KHONG_KHI_KEM": "không khí kém",
    "NGUY_HIEM": "nguy hiểm (rò gas)",
    "LOI_CAM_BIEN": "lỗi cảm biến",
}
NUMERIC = {"gas", "co2", "fan", "wifi_rssi"}
MERGE_GAP_S = 120  # gộp các lần báo động cùng loại cách nhau dưới 2 phút

mcp = MCPServer(
    "airgas",
    instructions=(
        "Trạm cảnh báo gas Gas-Monitor-01 (ESP32, MQ-2 đo gas, MQ-135 đo chất lượng không khí, "
        "van gas servo, quạt hút). Dùng get_status cho số liệu hiện tại, get_history cho xu hướng, "
        "get_alarm_events để biết đã có báo động hay chưa. control_device điều khiển thiết bị thật."
    ),
)

_client: httpx.AsyncClient | None = None
_entity_cache: tuple[float, dict[str, str]] = (0.0, {})


def _http() -> httpx.AsyncClient:
    global _client
    if not HA_TOKEN:
        raise ToolError("Chưa có HA_TOKEN trong ai-agent/.env (tạo Long-lived access token trong hồ sơ Home Assistant).")
    if _client is None:
        _client = httpx.AsyncClient(base_url=HA_URL, headers={"Authorization": f"Bearer {HA_TOKEN}"}, timeout=10)
    return _client


async def _request(method: str, path: str, **kw: Any) -> Any:
    """Gọi REST API của HA. Lỗi dự kiến ném ToolError để thông báo tới được client."""
    try:
        r = await _http().request(method, path, **kw)
    except httpx.HTTPError as e:
        raise ToolError(f"Không kết nối được Home Assistant ở {HA_URL}: {e}") from e
    if r.status_code == 401:
        raise ToolError("Home Assistant từ chối token (401). Kiểm tra HA_TOKEN.")
    if r.status_code >= 400:
        raise ToolError(f"Home Assistant trả lỗi {r.status_code} cho {path}: {r.text[:200]}")
    return r.json()


async def _get(path: str, params: dict[str, Any] | None = None) -> Any:
    return await _request("GET", path, params=params)


async def _states() -> list[dict]:
    return await _get("/api/states")


async def _entity_ids(states: list[dict] | None = None) -> dict[str, str]:
    """Map khóa -> entity_id, tìm theo friendly_name. Lưu tạm 5 phút."""
    global _entity_cache
    if states is None and time.monotonic() - _entity_cache[0] < 300 and _entity_cache[1]:
        return _entity_cache[1]
    states = states if states is not None else await _states()
    wanted = {**ENTITIES, **{f"btn_{k}": ("button", n) for k, n in BUTTONS.items()}}
    found: dict[str, str] = {}
    for s in states:
        eid = s["entity_id"]
        fname = (s.get("attributes") or {}).get("friendly_name", "")
        for key, (domain, name) in wanted.items():
            if key not in found and eid.startswith(domain + ".") and (fname == name or fname.endswith(" " + name)):
                found[key] = eid
    if not found:
        raise ToolError("Không tìm thấy thực thể nào của trạm gas trong Home Assistant. Node-RED đã gửi MQTT discovery chưa?")
    _entity_cache = (time.monotonic(), found)
    return found


def _num(v: Any) -> float | None:
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def _local(ts: str | None) -> str | None:
    if not ts:
        return None
    return datetime.fromisoformat(ts).astimezone(VN_TZ).strftime("%d/%m %H:%M:%S")


def _clean(v: Any) -> Any:
    return None if v in (None, "unknown", "unavailable", "None", "") else v


@mcp.tool(annotations=ToolAnnotations(title="Trạng thái hiện tại", read_only_hint=True, open_world_hint=False))
async def get_status() -> dict:
    """Số liệu hiện tại của trạm gas: nồng độ gas (ppm), CO2 tương đương (ppm), mức cảnh báo,
    có đang báo động rò gas không, van gas đóng/mở, % quạt hút, còi, firmware, sóng Wi-Fi."""
    states = await _states()
    ids = await _entity_ids(states)
    by_id = {s["entity_id"]: s for s in states}

    def st(key: str) -> dict | None:
        return by_id.get(ids.get(key, ""))

    def val(key: str) -> Any:
        s = st(key)
        return _clean(s["state"]) if s else None

    gas_state = st("gas")
    level = val("level")
    fan = _num(val("fan"))
    return {
        "time_now": datetime.now(VN_TZ).strftime("%d/%m/%Y %H:%M:%S"),
        "device_online": bool(gas_state) and gas_state["state"] != "unavailable",
        "gas_ppm": _num(val("gas")),
        "gas_threshold_ppm": GAS_THRESHOLD_PPM,
        "co2_ppm": _num(val("co2")),
        "co2_threshold_ppm": CO2_THRESHOLD_PPM,
        "level": level,
        "level_text": LEVEL_TEXT.get(level or "", level),
        "gas_alarm": val("gas_alarm") == "on",
        "valve": val("valve"),
        "fan_percent": int(fan) if fan is not None else None,
        "buzzer": val("buzzer"),
        "firmware": val("firmware"),
        "wifi_rssi_dbm": _num(val("wifi_rssi")),
        "last_update": _local(gas_state.get("last_updated")) if gas_state else None,
        "note": "ppm là ước tính từ cảm biến giá rẻ, chỉ dùng để cảnh báo sớm. null nghĩa là chưa có dữ liệu.",
    }


async def _history(key: str, hours: int) -> list[dict]:
    ids = await _entity_ids()
    if key not in ids:
        raise ToolError(f"Không tìm thấy thực thể cho '{key}' trong Home Assistant.")
    end = datetime.now(timezone.utc)
    start = end - timedelta(hours=hours)
    data = await _get(
        f"/api/history/period/{start.isoformat()}",
        {"filter_entity_id": ids[key], "end_time": end.isoformat(), "minimal_response": "", "no_attributes": ""},
    )
    rows = data[0] if data else []
    return [{"state": r["state"], "t": r.get("last_changed") or r.get("last_updated")} for r in rows]


@mcp.tool(annotations=ToolAnnotations(title="Lịch sử số liệu", read_only_hint=True, open_world_hint=False))
async def get_history(
    metric: Literal["gas", "co2", "fan", "wifi_rssi", "level", "valve", "gas_alarm"] = "gas",
    hours: int = 24,
) -> dict:
    """Lịch sử một số liệu trong `hours` giờ gần nhất (1–168). Với số (gas, co2, fan, wifi_rssi):
    trả min/max/trung bình/giá trị cuối và tối đa 24 điểm trung bình theo khoảng thời gian.
    Với trạng thái (level, valve, gas_alarm): trả danh sách các lần thay đổi."""
    hours = max(1, min(168, hours))
    rows = await _history(metric, hours)
    if metric in NUMERIC:
        pts = [(datetime.fromisoformat(r["t"]), v) for r in rows if (v := _num(r["state"])) is not None]
        if not pts:
            return {"metric": metric, "hours": hours, "samples": 0, "note": "Không có dữ liệu trong khoảng này."}
        vals = [v for _, v in pts]
        t0, span = pts[0][0], max((pts[-1][0] - pts[0][0]).total_seconds(), 1)
        buckets: dict[int, list[float]] = {}
        for t, v in pts:
            buckets.setdefault(min(23, int((t - t0).total_seconds() / span * 24)), []).append(v)
        series = [
            {"from": (t0 + timedelta(seconds=span * i / 24)).astimezone(VN_TZ).strftime("%d/%m %H:%M"), "avg": round(statistics.fmean(b), 1)}
            for i, b in sorted(buckets.items())
        ]
        return {
            "metric": metric, "hours": hours, "samples": len(vals),
            "min": round(min(vals), 1), "max": round(max(vals), 1), "avg": round(statistics.fmean(vals), 1),
            "last": round(vals[-1], 1), "series": series,
        }
    changes, prev = [], None
    for r in rows:
        s = _clean(r["state"])
        if s != prev:
            changes.append({"time": _local(r["t"]), "state": LEVEL_TEXT.get(s, s) if metric == "level" else s})
            prev = s
    return {"metric": metric, "hours": hours, "changes": changes[-40:], "total_changes": len(changes)}


@mcp.tool(annotations=ToolAnnotations(title="Các lần báo động", read_only_hint=True, open_world_hint=False))
async def get_alarm_events(hours: int = 24) -> dict:
    """Các đợt báo động trong `hours` giờ gần nhất (1–168): rò gas và không khí kém, kèm giờ bắt đầu,
    giờ kết thúc, số phút. Các lần cùng loại cách nhau dưới 2 phút được gộp thành một đợt (`flaps` là
    số lần bật lại trong đợt, nhiều nghĩa là số đo dao động quanh ngưỡng). Dùng để trả lời 'hôm nay có rò gas không?'."""
    hours = max(1, min(168, hours))
    rows = await _history("level", hours)
    now = datetime.now(timezone.utc)
    events, cur = [], None
    for r in rows:
        s = _clean(r["state"])
        if s is None:  # unknown/unavailable (mất kết nối) không cắt đợt đang diễn ra
            continue
        t = datetime.fromisoformat(r["t"])
        if cur and s != cur["level"]:
            cur["end"] = t
            events.append(cur)
            cur = None
        if cur is None and s in ("NGUY_HIEM", "KHONG_KHI_KEM"):
            cur = {"level": s, "start": t, "end": None, "flaps": 0}
    if cur:
        events.append(cur)
    merged: list[dict] = []
    for e in events:
        last = next((m for m in reversed(merged) if m["level"] == e["level"]), None)
        if last and last["end"] and (e["start"] - last["end"]).total_seconds() <= MERGE_GAP_S:
            last["end"] = e["end"]
            last["flaps"] += 1
        else:
            merged.append(e)
    events = merged
    out = [
        {
            "type": LEVEL_TEXT[e["level"]],
            "start": e["start"].astimezone(VN_TZ).strftime("%d/%m %H:%M:%S"),
            "end": e["end"].astimezone(VN_TZ).strftime("%d/%m %H:%M:%S") if e["end"] else "đang diễn ra",
            "minutes": round(((e["end"] or now) - e["start"]).total_seconds() / 60, 1),
            "flaps": e["flaps"],
        }
        for e in events
    ]
    return {
        "hours": hours,
        "gas_leak_count": sum(e["level"] == "NGUY_HIEM" for e in events),
        "poor_air_count": sum(e["level"] == "KHONG_KHI_KEM" for e in events),
        "events": out[-30:],
    }


ACTION_TEXT = {
    "open_valve": "mở van gas",
    "close_valve": "khóa van gas",
    "test_fan": "chạy thử quạt hút 60% trong 10 giây",
    "test_buzzer": "cho còi kêu thử 3 giây",
    "recalibrate": "hiệu chuẩn lại cảm biến (cần không khí sạch)",
}


@mcp.tool(
    annotations=ToolAnnotations(
        title="Điều khiển thiết bị", read_only_hint=False, destructive_hint=True, idempotent_hint=False, open_world_hint=False
    )
)
async def control_device(action: Literal["open_valve", "close_valve", "test_fan", "test_buzzer", "recalibrate"]) -> str:
    """Điều khiển thiết bị thật qua Home Assistant: open_valve (mở van gas), close_valve (khóa van gas),
    test_fan (chạy thử quạt 10 giây), test_buzzer (còi kêu thử 3 giây, firmware 1.5.0+), recalibrate (hiệu chuẩn cảm biến, chỉ làm khi không khí sạch).
    Mở van bị từ chối khi đang báo động rò gas."""
    states = await _states()
    ids = await _entity_ids(states)
    if action == "open_valve":
        alarm = next((s for s in states if s["entity_id"] == ids.get("gas_alarm")), None)
        if alarm and alarm["state"] == "on":
            return "TỪ CHỐI: đang báo động rò gas nên không mở van. Hãy chờ nồng độ gas về mức an toàn."
    eid = ids.get(f"btn_{action}")
    if not eid:
        raise ToolError(f"Không tìm thấy nút '{BUTTONS[action]}' trong Home Assistant.")
    await _request("POST", "/api/services/button/press", json={"entity_id": eid})
    return f"Đã gửi lệnh {ACTION_TEXT[action]} qua Home Assistant ({eid}). Thiết bị cần vài giây để thực hiện; gọi get_status để kiểm tra kết quả."


def _serve_http() -> None:
    """Streamable HTTP ở /mcp, bắt buộc header Authorization: Bearer <MCP_AUTH_TOKEN>."""
    import hmac

    import uvicorn
    from mcp.server.transport_security import TransportSecuritySettings

    token = os.environ.get("MCP_AUTH_TOKEN", "")
    if len(token) < 16:
        raise SystemExit("Chạy qua HTTP cần MCP_AUTH_TOKEN (ít nhất 16 ký tự) vì server điều khiển được van gas.")
    expected = f"Bearer {token}".encode()

    class BearerAuth:
        def __init__(self, app: Any) -> None:
            self.app = app

        async def __call__(self, scope: dict, receive: Any, send: Any) -> None:
            if scope["type"] == "http":
                got = dict(scope["headers"]).get(b"authorization", b"")
                if not hmac.compare_digest(got, expected):
                    await send({"type": "http.response.start", "status": 401,
                                "headers": [(b"content-type", b"application/json"), (b"www-authenticate", b"Bearer")]})
                    await send({"type": "http.response.body", "body": b'{"error":"unauthorized"}'})
                    return
            await self.app(scope, receive, send)

    # Host header đến từ tên container hoặc tên Tailscale, nên tắt kiểm tra DNS rebinding;
    # token Bearer ở trên mới là lớp bảo vệ.
    app = mcp.streamable_http_app(host="0.0.0.0", transport_security=TransportSecuritySettings(enable_dns_rebinding_protection=False))
    app.add_middleware(BearerAuth)
    host, port = os.environ.get("MCP_HOST", "0.0.0.0"), int(os.environ.get("MCP_PORT", "8000"))
    print(f"airgas MCP server http://{host}:{port}/mcp, Home Assistant: {HA_URL}", file=sys.stderr)
    uvicorn.run(app, host=host, port=port, log_level="warning")


if __name__ == "__main__":
    if os.environ.get("MCP_TRANSPORT", "stdio") == "streamable-http":
        _serve_http()
    else:
        print(f"airgas MCP server (stdio), Home Assistant: {HA_URL}", file=sys.stderr)
        mcp.run(transport="stdio")
