"""Web UI end-to-end (spec section 11, plan F8).

Run: fw/scripts/web.sh (device backend in CI) or
     python3 -m pytest fw/tests/web --web-backend=mock (host build only).
"""
import json
import urllib.request


def get_json(base_url, path):
    with urllib.request.urlopen(base_url + path, timeout=5) as r:
        return json.loads(r.read().decode())


def wait_msg(page, ok=True):
    """Waits for the status line; on the wrong outcome fails with its text."""
    page.wait_for_selector("#msg.ok, #msg.bad", timeout=10000)
    cls = page.get_attribute("#msg", "class")
    text = page.inner_text("#msg")
    assert cls == ("ok" if ok else "bad"), text
    return text


def click_msg(page, selector, ok=True):
    """Clicks and waits for the status line of this action, not a stale one."""
    page.evaluate("() => { const m = document.querySelector('#msg'); m.className = ''; m.textContent = ''; }")
    page.click(selector)
    return wait_msg(page, ok)


def tab(page, name):
    page.click(f"#tabs [data-tab={name}]")


def test_screen_list_shows_factory_set(page, base_url):
    cfg = get_json(base_url, "/api/screens")
    cards = page.locator("#screen-list .card")
    assert cards.count() == len(cfg["screens"])
    assert page.locator("#screen-list .card .badge").count() == 1
    # every card has a drawn preview
    assert page.locator("#screen-list canvas.mini").count() == len(cfg["screens"])
    assert "по умолчанию" in page.locator("#screen-list .card[data-id=main]").inner_text()
    assert "in.co2 > 1000" in page.locator("#screen-list .card[data-id=stuffy] .summary").inner_text()


def test_drag_item_onto_canvas_and_save(page, base_url):
    page.click("#btn-new")
    tab(page, "editor")
    before = 0
    page.locator("#palette .pal-item[data-type=clock]").drag_to(
        page.locator("#ed-canvas"), target_position={"x": 8 * 40, "y": 8 * 2})
    assert page.locator("#props h3").inner_text() == "Часы"
    assert page.locator("#dirty").is_visible()
    click_msg(page, "#btn-save")
    items = get_json(base_url, "/api/screens")["screens"][-1]["items"]
    assert len(items) == before + 1
    assert items[-1]["type"] == "clock" and items[-1]["x"] == 40
    assert not page.locator("#dirty").is_visible()


def test_validation_errors_are_shown(page):
    tab(page, "editor")
    page.locator("#ed-canvas").click(position={"x": 8 * 50, "y": 8 * 5})
    page.locator("#palette .pal-item[data-type=text]").dblclick()
    page.locator("#props label.prop", has_text="x").locator("input").fill("200")
    page.locator("#props label.prop", has_text="x").locator("input").press("Enter")
    page.wait_for_selector("#ed-errors.bad", timeout=5000)
    assert "items" in page.locator("#ed-errors").inner_text()
    click_msg(page, "#btn-save", ok=False)
    assert "Не сохранено" in page.locator("#msg").inner_text()


def test_undo_redo(page):
    tab(page, "editor")
    n0 = page.evaluate("WSApp.state.cfg.screens[WSApp.state.cur].items.length")
    page.locator("#palette .pal-item[data-type=sep]").dblclick()
    assert page.evaluate("WSApp.state.cfg.screens[WSApp.state.cur].items.length") == n0 + 1
    page.click("#btn-undo")
    assert page.evaluate("WSApp.state.cfg.screens[WSApp.state.cur].items.length") == n0
    page.click("#btn-redo")
    assert page.evaluate("WSApp.state.cfg.screens[WSApp.state.cur].items.length") == n0 + 1


def test_rule_editor_updates_summary(page):
    page.locator("#screen-list .card[data-id=stuffy] button", has_text="Правило").click()
    prio = page.locator("#rule-editor label.prop", has_text="Приоритет").locator("input")
    assert prio.input_value() == "70"
    prio.fill("80")
    prio.press("Enter")
    assert "приоритет 80" in page.locator("#rule-summary").inner_text()
    page.click("#rule-editor button:has-text('Добавить условие')")
    assert page.locator("#rule-editor table.conds tbody tr").count() == 2


def test_simulator_picks_screen_by_rules(page):
    tab(page, "sim")
    co2 = page.locator("#sim-vars label.var", has_text="in.co2").locator("input")
    page.evaluate("WSApp.state.sim.sod = 12 * 60")
    co2.fill("1500")
    co2.press("Enter")
    assert "Душно" in page.locator("#sim-reason").inner_text()
    co2.fill("600")
    co2.press("Enter")
    assert "Душно" not in page.locator("#sim-reason").inner_text()
    page.click("#btn-sim-day")
    assert page.locator("#sim-day tbody tr").count() >= 1
    assert "Смен экрана за сутки" in page.locator("#sim-day").inner_text()


def test_picto_editor(page, base_url):
    tab(page, "pictos")
    page.click("#pic-new")
    page.locator("#pic-grid").click(position={"x": 16, "y": 16})   # dot (0,0)
    page.locator("#pic-grid").click(position={"x": 48, "y": 16})   # dot (1,0)
    rows = page.evaluate("WSApp.state.cfg.pictos[0].rows")
    assert rows[0] == "0600" and all(r == "0000" for r in rows[1:])
    page.click(".pic-tools [data-op=invert]")
    assert page.evaluate("WSApp.state.cfg.pictos[0].rows[0]") == "01ff"
    click_msg(page, "#btn-save")
    saved = get_json(base_url, "/api/screens")
    assert saved["pictos"][0]["rows"][0] == "01ff"
    assert any(p["name"] == saved["pictos"][0]["name"] for p in get_json(base_url, "/api/glyphs")["user_pictos"])


def test_settings_roundtrip(page, base_url):
    tab(page, "settings")
    page.wait_for_function("document.querySelector('[name=\"mqtt.port\"]').value !== ''")
    psk = page.locator("[name='wifi.psk']")
    assert psk.input_value() == ""
    page.fill("[name='metar.icao']", "unnt")
    click_msg(page, "#settings-form button[type=submit]", ok=False)
    assert "metar.icao" in page.locator("#msg").inner_text()
    page.fill("[name='metar.icao']", "UNNT")
    page.fill("[name='mqtt.host']", "broker.lan")
    click_msg(page, "#settings-form button[type=submit]")
    s = get_json(base_url, "/api/settings")
    assert s["mqtt.host"] == "broker.lan"
    assert "wifi.psk" not in s  # secrets never leave the device


def test_wifi_scan(page):
    tab(page, "settings")
    page.click("#btn-scan")
    page.wait_for_function("document.querySelectorAll('#ssid-list option').length > 0", timeout=20000)


def test_preview_on_sign(page, base_url, backend):
    tab(page, "editor")
    click_msg(page, "#btn-preview")
    if backend == "mock":
        log = get_json(base_url, "/__mock/log")
        assert any(e["path"] == "/api/display/preview" for e in log)


def test_status_page(page):
    tab(page, "status")
    page.wait_for_selector("#vars-table tbody tr")
    assert "in.co2" in page.locator("#vars-table").inner_text()
    assert page.locator("#status-list dt", has_text="Версия").count() == 1


def test_factory_and_rollback(page, base_url):
    page.locator("#palette .pal-item").count()  # page ready
    page.evaluate("WSApp.state.cfg.screens[0].name = 'X'")
    click_msg(page, "#btn-save")
    assert get_json(base_url, "/api/screens")["screens"][0]["name"] == "X"
    page.click("#btn-rollback")
    page.wait_for_function("document.querySelector('#msg').textContent.includes('предыдущая')")
    assert get_json(base_url, "/api/screens")["screens"][0]["name"] == "Душно"
