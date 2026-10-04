/* Variables as JSON for the web API and the simulator, see ws/vars.h. */
#include <string.h>

#include <ws/json.h>
#include <ws/sign.h>
#include <ws/vars.h>

static const char *type_name(int t)
{
	static const char *const n[] = {"num", "bool", "cond", "dir", "str", "hours", "any"};

	return t >= 0 && t <= WS_VT_ANY ? n[t] : "";
}

static void write_value(struct ws_jw *w, const struct ws_value *v)
{
	if (!v->known) {
		ws_jw_null(w);
		return;
	}
	switch (v->type) {
	case WS_VT_BOOL:
		ws_jw_bool(w, v->num != 0);
		break;
	case WS_VT_COND:
		ws_jw_str(w, ws_cond_name(v->num));
		break;
	case WS_VT_DIR:
		ws_jw_str(w, ws_dir_name(v->num));
		break;
	case WS_VT_STR:
		ws_jw_str(w, v->str);
		break;
	case WS_VT_HOURS:
		ws_jw_arr(w);
		for (int i = 0; v->hours && i < v->hours->n; i++) {
			ws_jw_arr(w);
			ws_jw_int(w, v->hours->t[i]);
			ws_jw_int(w, v->hours->pop[i]);
			ws_jw_arr_end(w);
		}
		ws_jw_arr_end(w);
		break;
	default:
		ws_jw_deci(w, v->num);
		break;
	}
}

int ws_vars_to_json(const struct ws_vars *vars, const struct ws_now *now, char *buf, size_t len)
{
	struct ws_jw w;

	ws_jw_init(&w, buf, len);
	ws_jw_obj(&w);
	ws_jw_kbool(&w, "fallback", ws_vars_fallback_active(vars, now));
	ws_jw_key(&w, "vars");
	ws_jw_arr(&w);
	for (int i = 0; i < WS_V_COUNT; i++) {
		const struct ws_var_info *inf = ws_var_info(i);
		struct ws_value v;

		ws_vars_get(vars, i, now, &v);
		ws_jw_obj(&w);
		ws_jw_kstr(&w, "name", inf->name);
		ws_jw_key(&w, "value");
		write_value(&w, &v);
		ws_jw_kstr(&w, "unit", inf->unit);
		ws_jw_kstr(&w, "src", ws_vsrc_name(v.src));
		ws_jw_kstr(&w, "type", type_name(inf->type));
		ws_jw_key(&w, "age");
		if (vars->v[i].set) {
			ws_jw_int(&w, now->mono - vars->v[i].updated);
		} else {
			ws_jw_null(&w);
		}
		ws_jw_obj_end(&w);
	}
	ws_jw_arr_end(&w);
	ws_jw_obj_end(&w);
	return w.ok ? (int)w.pos : -1;
}

int ws_vars_apply_json(struct ws_vars *vars, const struct ws_json *j, int obj, int64_t mono)
{
	int n = 0;

	if (!ws_json_is(j, obj, WS_J_OBJ)) {
		return 0;
	}
	for (int k = ws_json_first(j, obj); k >= 0; k = ws_json_next(j, obj, k)) {
		char name[24], s[WS_VAR_STR_LEN];
		int v = k + 1;
		int32_t d;
		bool b;

		if (ws_json_str(j, k, name, sizeof(name)) < 0) {
			continue;
		}
		int id = ws_var_lookup(name);

		if (id < 0) {
			continue;
		}
		const struct ws_var_info *inf = ws_var_info(id);

		if (ws_json_is(j, v, WS_J_NULL)) {
			ws_vars_clear(vars, id);
			n++;
			continue;
		}
		switch (inf->type) {
		case WS_VT_BOOL:
			if (ws_json_bool(j, v, &b)) {
				ws_vars_set_num(vars, id, b, mono);
				n++;
			}
			break;
		case WS_VT_COND:
		case WS_VT_DIR:
			if (ws_json_str(j, v, s, sizeof(s)) > 0) {
				int e = inf->type == WS_VT_COND ? ws_cond_parse(s)
								: ws_dir_parse(s);

				if (e >= 0) {
					ws_vars_set_num(vars, id, e, mono);
					n++;
				}
			}
			break;
		case WS_VT_STR:
			if (ws_json_str(j, v, s, sizeof(s)) >= 0) {
				ws_vars_set_str(vars, id, s, mono);
				n++;
			}
			break;
		case WS_VT_HOURS: {
			struct ws_hours h = {0};

			for (int e = ws_json_first(j, v);
			     e >= 0 && ws_json_is(j, v, WS_J_ARR) && h.n < WS_HOURS_MAX;
			     e = ws_json_next(j, v, e)) {
				int32_t t, pop;

				if (!ws_json_deci(j, ws_json_at(j, e, 0), &t) ||
				    !ws_json_int(j, ws_json_at(j, e, 1), &pop)) {
					break;
				}
				h.t[h.n] = (int16_t)((t >= 0 ? t + 5 : t - 5) / 10);
				h.pop[h.n++] = (uint8_t)(pop < 0 ? 0 : pop > 100 ? 100 : pop);
			}
			ws_vars_set_hours(vars, &h, mono);
			n++;
			break;
		}
		default:
			if (ws_json_deci(j, v, &d)) {
				ws_vars_set_num(vars, id, d, mono);
				n++;
			} else if (inf->type == WS_VT_ANY && ws_json_str(j, v, s, sizeof(s)) >= 0) {
				ws_vars_set_str(vars, id, s, mono);
				n++;
			}
			break;
		}
		/* ages are computed, let the simulator set them through the receive time */
		if (id == WS_V_FC_AGE && ws_json_deci(j, v, &d)) {
			vars->fc_rx = mono - (d / 10) * 60;
			vars->fc_ts = 0;
		}
		if (id == WS_V_OBS_AGE && ws_json_deci(j, v, &d)) {
			vars->obs_rx = mono - (d / 10) * 60;
			vars->obs_ts = 0;
		}
	}
	return n;
}
