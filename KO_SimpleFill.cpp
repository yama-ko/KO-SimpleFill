/*
	KO SimpleFill

	Fills the layer with a solid color using selectable blend modes and amount.

	Popup layout (1-indexed, separators count):
	  1=Normal | 2=sep | 3=Add | 4=Negative Add | 5=Multiply | 6=Screen
	  7=sep | 8=Overlay | 9=Hard Light | 10=sep | 11=Lighten | 12=Darken | 13=Difference

	Add / Negative Add use scale approach (amount scales fill contribution).
	All other modes use lerp (amount lerps src toward blend result).
*/

#include "KO_SimpleFill.h"

// -------------------------------------------------------------------
// Popup value ↁEinternal BlendMode
// -------------------------------------------------------------------

static A_long PopupToBlendMode(A_long v)
{
	switch (v) {
		case 3:  return BLEND_ADD;
		case 4:  return BLEND_NEGATIVE_ADD;
		case 5:  return BLEND_MULTIPLY;
		case 6:  return BLEND_SCREEN;
		case 8:  return BLEND_OVERLAY;
		case 9:  return BLEND_HARD_LIGHT;
		case 11: return BLEND_LIGHTEN;
		case 12: return BLEND_DARKEN;
		case 13: return BLEND_DIFFERENCE;
		default: return BLEND_NORMAL; // 1=Normal, separators fall here
	}
}

// -------------------------------------------------------------------
// Blend math (normalized [0,1])
// Add/NegAdd: scale approach. Others: lerp approach.
// -------------------------------------------------------------------

static inline PF_FpLong Clamp01(PF_FpLong v)
{
	return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

static PF_FpLong ApplyBlend(PF_FpLong s, PF_FpLong f, A_long mode, PF_FpLong amount)
{
	switch (mode) {
		// --- Scale modes ---
		case BLEND_ADD:
			return Clamp01(s + f * amount);
		case BLEND_NEGATIVE_ADD:
			return Clamp01(s - (1.0 - f) * amount);

		// --- Lerp modes ---
		case BLEND_MULTIPLY:
			return s * f * amount + s * (1.0 - amount);
		case BLEND_SCREEN: {
			PF_FpLong b = 1.0 - (1.0 - s) * (1.0 - f);
			return b * amount + s * (1.0 - amount);
		}
		case BLEND_OVERLAY: {
			PF_FpLong b = s < 0.5 ? 2.0*s*f : 1.0 - 2.0*(1.0-s)*(1.0-f);
			return b * amount + s * (1.0 - amount);
		}
		case BLEND_HARD_LIGHT: {
			PF_FpLong b = f < 0.5 ? 2.0*s*f : 1.0 - 2.0*(1.0-s)*(1.0-f);
			return b * amount + s * (1.0 - amount);
		}
		case BLEND_LIGHTEN: {
			PF_FpLong b = s > f ? s : f;
			return b * amount + s * (1.0 - amount);
		}
		case BLEND_DARKEN: {
			PF_FpLong b = s < f ? s : f;
			return b * amount + s * (1.0 - amount);
		}
		case BLEND_DIFFERENCE: {
			PF_FpLong diff = s - f;
			PF_FpLong b = diff < 0.0 ? -diff : diff;
			return b * amount + s * (1.0 - amount);
		}
		default: // BLEND_NORMAL
			return f * amount + s * (1.0 - amount);
	}
}

// -------------------------------------------------------------------
// Pixel callbacks
// -------------------------------------------------------------------

static PF_Err
FillFunc8(void *refcon, A_long xL, A_long yL, PF_Pixel8 *inP, PF_Pixel8 *outP)
{
	FillInfoP fiP = reinterpret_cast<FillInfoP>(refcon);
	if (!fiP) return PF_Err_NONE;

	PF_FpLong s_r = inP->red   / 255.0, f_r = fiP->color.red   / 255.0;
	PF_FpLong s_g = inP->green / 255.0, f_g = fiP->color.green / 255.0;
	PF_FpLong s_b = inP->blue  / 255.0, f_b = fiP->color.blue  / 255.0;

	outP->red   = (A_u_char)(ApplyBlend(s_r, f_r, fiP->blendMode, fiP->amount) * 255.0 + 0.5);
	outP->green = (A_u_char)(ApplyBlend(s_g, f_g, fiP->blendMode, fiP->amount) * 255.0 + 0.5);
	outP->blue  = (A_u_char)(ApplyBlend(s_b, f_b, fiP->blendMode, fiP->amount) * 255.0 + 0.5);
	outP->alpha = fiP->invertAlpha ? (A_u_char)(PF_MAX_CHAN8 - inP->alpha) : inP->alpha;
	return PF_Err_NONE;
}

static PF_Err
FillFunc16(void *refcon, A_long xL, A_long yL, PF_Pixel16 *inP, PF_Pixel16 *outP)
{
	FillInfoP fiP = reinterpret_cast<FillInfoP>(refcon);
	if (!fiP) return PF_Err_NONE;

	PF_FpLong max16 = (PF_FpLong)PF_MAX_CHAN16;
	PF_FpLong s_r = inP->red   / max16, f_r = fiP->color.red   / 255.0;
	PF_FpLong s_g = inP->green / max16, f_g = fiP->color.green / 255.0;
	PF_FpLong s_b = inP->blue  / max16, f_b = fiP->color.blue  / 255.0;

	outP->red   = (A_u_short)(ApplyBlend(s_r, f_r, fiP->blendMode, fiP->amount) * max16 + 0.5);
	outP->green = (A_u_short)(ApplyBlend(s_g, f_g, fiP->blendMode, fiP->amount) * max16 + 0.5);
	outP->blue  = (A_u_short)(ApplyBlend(s_b, f_b, fiP->blendMode, fiP->amount) * max16 + 0.5);
	outP->alpha = fiP->invertAlpha ? (A_u_short)(PF_MAX_CHAN16 - inP->alpha) : inP->alpha;
	return PF_Err_NONE;
}

// -------------------------------------------------------------------
// Effect entry points
// -------------------------------------------------------------------

static PF_Err
About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	suites.ANSICallbacksSuite1()->sprintf(
		out_data->return_msg,
		"KO SimpleFill v%d.%d\r"
		"Solid color fill with blend modes.",
		MAJOR_VERSION, MINOR_VERSION);
	return PF_Err_NONE;
}

static PF_Err
GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	out_data->my_version = PF_VERSION(
		MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
	out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE;
	return PF_Err_NONE;
}

static PF_Err
ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err      err = PF_Err_NONE;
	PF_ParamDef def;

	// Fill Color
	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Fill Color",
		PF_MAX_CHAN8, PF_MAX_CHAN8, PF_MAX_CHAN8,
		COLOR_DISK_ID);

	// Blend Mode popup
	// Separators ((-) count as index slots.
	// Total 13 items: Normal | sep | Add | Negative Add | Multiply | Screen
	//               | sep | Overlay | Hard Light | sep | Lighten | Darken | Difference
	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Blend Mode",
		POPUP_TOTAL_ITEMS,
		1, // default: Normal
		"Normal"
		"|(-"
		"|Add|Negative Add|Multiply|Screen"
		"|(-"
		"|Overlay|Hard Light"
		"|(-"
		"|Lighten|Darken|Difference",
		BLEND_MODE_DISK_ID);

	// Amount
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Amount",
		0, 100, 0, 100, 100,
		PF_Precision_TENTHS, 0, 0,
		AMOUNT_DISK_ID);

	// Invert Alpha checkbox
	AEFX_CLR_STRUCT(def);
	def.param_type      = PF_Param_CHECKBOX;
	def.u.bd.value      = FALSE;
	def.u.bd.dephault   = FALSE;
	def.uu.id           = INVERT_ALPHA_DISK_ID;
	PF_STRNNCPY(def.PF_DEF_NAME, "Invert Alpha", sizeof(def.PF_DEF_NAME));
	ERR(PF_ADD_PARAM(in_data, -1, &def));

	out_data->num_params = FILL_NUM_PARAMS;
	return err;
}

static PF_Err
Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	FillInfo fi;
	AEFX_CLR_STRUCT(fi);
	fi.color       = params[FILL_COLOR]->u.cd.value;
	fi.blendMode   = PopupToBlendMode(params[FILL_BLEND_MODE]->u.pd.value);
	fi.amount      = params[FILL_AMOUNT]->u.fs_d.value / 100.0;
	fi.invertAlpha = params[FILL_INVERT_ALPHA]->u.bd.value ? TRUE : FALSE;

	A_long linesL = output->extent_hint.bottom - output->extent_hint.top;

	if (PF_WORLD_IS_DEEP(output)) {
		ERR(suites.Iterate16Suite2()->iterate(
			in_data, 0, linesL, &params[FILL_INPUT]->u.ld,
			NULL, (void*)&fi, FillFunc16, output));
	} else {
		ERR(suites.Iterate8Suite2()->iterate(
			in_data, 0, linesL, &params[FILL_INPUT]->u.ld,
			NULL, (void*)&fi, FillFunc8, output));
	}
	return err;
}

extern "C" DllExport
PF_Err PluginDataEntryFunction2(
	PF_PluginDataPtr  inPtr,
	PF_PluginDataCB2  inPluginDataCallBackPtr,
	SPBasicSuite     *inSPBasicSuitePtr,
	const char       *inHostName,
	const char       *inHostVersion)
{
	PF_Err result = PF_Err_INVALID_CALLBACK;
	result = PF_REGISTER_EFFECT_EXT2(
		inPtr,
		inPluginDataCallBackPtr,
		"KO SimpleFill",
		"KO_SimpleFill",
		"yama-ko.net",
		AE_RESERVED_INFO,
		"EffectMain",
		"https://github.com");
	return result;
}

PF_Err
EffectMain(
	PF_Cmd       cmd,
	PF_InData   *in_data,
	PF_OutData  *out_data,
	PF_ParamDef *params[],
	PF_LayerDef *output,
	void        *extra)
{
	PF_Err err = PF_Err_NONE;
	try {
		switch (cmd) {
			case PF_Cmd_ABOUT:        err = About(in_data, out_data, params, output);       break;
			case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(in_data, out_data, params, output); break;
			case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data, params, output); break;
			case PF_Cmd_RENDER:       err = Render(in_data, out_data, params, output);       break;
		}
	}
	catch (PF_Err &thrown_err) { err = thrown_err; }
	return err;
}
