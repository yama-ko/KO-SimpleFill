/*
	KO SimpleFill

	Fills the layer with a solid color using selectable blend modes and amount.

	Blend Mode popup — aligned with KO BandFill (minus None / Stencil / Silhouette).
	1-indexed, separators count:
	  1=Normal | 2=sep | 3=Add | 4=Negative Add | 5=Multiply | 6=Screen
	  7=sep | 8=Overlay | 9=Soft Light | 10=Hard Light | 11=Linear Light
	  12=sep | 13=Lighten | 14=Darken | 15=Difference
	  16=sep | 17=Hue | 18=Saturation | 19=Color | 20=Luminosity
	  21=sep | 22=Dither | 23=Dither Only

	Add / Negative Add use scale approach (amount scales fill contribution).
	Hue/Saturation/Color/Luminosity are non-separable (need all 3 channels).
	Dither / Dither Only are probabilistic (per-pixel hash < amount).
	All other modes use lerp (amount lerps src toward blend result).

	Rendering: SmartRender (8/16/32 bpc, layer/composite-options masks) with a
	legacy Render fallback for hosts that don't drive SmartRender (e.g. Premiere).
*/

#include "KO_SimpleFill.h"

#include <cstdint>
#include <cmath>
#include <new>

// -------------------------------------------------------------------
// Popup value -> internal BlendMode
// -------------------------------------------------------------------

static A_long PopupToBlendMode(A_long v)
{
	switch (v) {
		case 3:  return BLEND_ADD;
		case 4:  return BLEND_NEGATIVE_ADD;
		case 5:  return BLEND_MULTIPLY;
		case 6:  return BLEND_SCREEN;
		case 8:  return BLEND_OVERLAY;
		case 9:  return BLEND_SOFT_LIGHT;
		case 10: return BLEND_HARD_LIGHT;
		case 11: return BLEND_LINEAR_LIGHT;
		case 13: return BLEND_LIGHTEN;
		case 14: return BLEND_DARKEN;
		case 15: return BLEND_DIFFERENCE;
		case 17: return BLEND_HUE;
		case 18: return BLEND_SATURATION;
		case 19: return BLEND_COLOR;
		case 20: return BLEND_LUMINOSITY;
		case 22: return BLEND_DITHER;
		case 23: return BLEND_DITHER_ONLY;
		default: return BLEND_NORMAL; // 1=Normal, separators fall here
	}
}

// -------------------------------------------------------------------
// Blend math (normalized [0,1]) — ported from KO BandFill
// -------------------------------------------------------------------

static inline PF_FpLong Clamp01(PF_FpLong v)
{
	return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

static uint32_t HashUI(uint32_t seed, uint32_t idx)
{
	uint32_t h = seed ^ (idx * 2654435761u);
	h ^= h >> 16;
	h *= 0x45d9f3bu;
	h ^= h >> 16;
	return h;
}

static PF_FpLong Hash01(uint32_t seed, uint32_t idx)
{
	return (PF_FpLong)(HashUI(seed, idx) & 0x7fffffffu) / (PF_FpLong)0x7fffffffu;
}

// Channel-independent modes. Add/NegAdd: scale approach. Others: lerp approach.
static PF_FpLong ApplyBlend(PF_FpLong s, PF_FpLong f, A_long mode, PF_FpLong amount)
{
	switch (mode) {
		case BLEND_ADD:          return Clamp01(s + f * amount);
		case BLEND_NEGATIVE_ADD: return Clamp01(s - (1.0 - f) * amount);
		case BLEND_MULTIPLY:   { PF_FpLong b = s * f;                                              return b*amount + s*(1.0-amount); }
		case BLEND_SCREEN:     { PF_FpLong b = 1.0 - (1.0-s)*(1.0-f);                              return b*amount + s*(1.0-amount); }
		case BLEND_OVERLAY:    { PF_FpLong b = s<0.5 ? 2.0*s*f : 1.0-2.0*(1.0-s)*(1.0-f);          return b*amount + s*(1.0-amount); }
		case BLEND_HARD_LIGHT: { PF_FpLong b = f<0.5 ? 2.0*s*f : 1.0-2.0*(1.0-s)*(1.0-f);          return b*amount + s*(1.0-amount); }
		case BLEND_LIGHTEN:    { PF_FpLong b = s>f ? s : f;                                        return b*amount + s*(1.0-amount); }
		case BLEND_DARKEN:     { PF_FpLong b = s<f ? s : f;                                        return b*amount + s*(1.0-amount); }
		case BLEND_DIFFERENCE: { PF_FpLong d = s-f; PF_FpLong b = d<0.0 ? -d : d;                  return b*amount + s*(1.0-amount); }
		case BLEND_SOFT_LIGHT: {
			PF_FpLong b;
			if (f <= 0.5) {
				b = s - (1.0 - 2.0*f) * s * (1.0 - s);
			} else {
				PF_FpLong g = (s <= 0.25) ? ((16.0*s - 12.0)*s + 4.0)*s : std::sqrt(s);
				b = s + (2.0*f - 1.0) * (g - s);
			}
			return b*amount + s*(1.0-amount);
		}
		case BLEND_LINEAR_LIGHT: { PF_FpLong b = Clamp01(s + 2.0*f - 1.0); return b*amount + s*(1.0-amount); }
		default:                 return f*amount + s*(1.0-amount); // BLEND_NORMAL
	}
}

// --- Non-separable (color component) helpers ---

static inline PF_FpLong BlendLum(PF_FpLong r, PF_FpLong g, PF_FpLong b)
{
	return 0.299*r + 0.587*g + 0.114*b;
}

static inline PF_FpLong BlendSat(PF_FpLong r, PF_FpLong g, PF_FpLong b)
{
	PF_FpLong mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
	PF_FpLong mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
	return mx - mn;
}

static void BlendClipColor(PF_FpLong &r, PF_FpLong &g, PF_FpLong &b)
{
	PF_FpLong l = BlendLum(r, g, b);
	PF_FpLong n = r < g ? (r < b ? r : b) : (g < b ? g : b);
	PF_FpLong x = r > g ? (r > b ? r : b) : (g > b ? g : b);
	if (n < 0.0) { PF_FpLong s = l / (l - n); r = l+(r-l)*s; g = l+(g-l)*s; b = l+(b-l)*s; }
	if (x > 1.0) { PF_FpLong s = (1.0-l) / (x-l); r = l+(r-l)*s; g = l+(g-l)*s; b = l+(b-l)*s; }
}

static void BlendSetLum(PF_FpLong &r, PF_FpLong &g, PF_FpLong &b, PF_FpLong l)
{
	PF_FpLong d = l - BlendLum(r, g, b);
	r += d; g += d; b += d;
	BlendClipColor(r, g, b);
}

static void BlendSetSat(PF_FpLong &r, PF_FpLong &g, PF_FpLong &b, PF_FpLong s)
{
	PF_FpLong *cmin, *cmid, *cmax;
	if      (r <= g && r <= b) { cmin=&r; if (g<=b){cmid=&g;cmax=&b;}else{cmid=&b;cmax=&g;} }
	else if (g <= r && g <= b) { cmin=&g; if (r<=b){cmid=&r;cmax=&b;}else{cmid=&b;cmax=&r;} }
	else                       { cmin=&b; if (r<=g){cmid=&r;cmax=&g;}else{cmid=&g;cmax=&r;} }
	if (*cmax > *cmin) { *cmid = (*cmid - *cmin) * s / (*cmax - *cmin); *cmax = s; }
	else               { *cmid = *cmax = 0.0; }
	*cmin = 0.0;
}

// Channel-coupled blend for color component modes; falls back to per-channel ApplyBlend.
static void ApplyBlendRGB(
	PF_FpLong sr, PF_FpLong sg, PF_FpLong sb,
	PF_FpLong fr, PF_FpLong fg, PF_FpLong fb,
	A_long mode, PF_FpLong amount,
	PF_FpLong *out_r, PF_FpLong *out_g, PF_FpLong *out_b)
{
	PF_FpLong br, bg, bb;
	switch (mode) {
		case BLEND_HUE: {
			br=fr; bg=fg; bb=fb;
			BlendSetSat(br, bg, bb, BlendSat(sr, sg, sb));
			BlendSetLum(br, bg, bb, BlendLum(sr, sg, sb));
			break;
		}
		case BLEND_SATURATION: {
			br=sr; bg=sg; bb=sb;
			BlendSetSat(br, bg, bb, BlendSat(fr, fg, fb));
			BlendSetLum(br, bg, bb, BlendLum(sr, sg, sb));
			break;
		}
		case BLEND_COLOR: {
			br=fr; bg=fg; bb=fb;
			BlendSetLum(br, bg, bb, BlendLum(sr, sg, sb));
			break;
		}
		case BLEND_LUMINOSITY: {
			br=sr; bg=sg; bb=sb;
			BlendSetLum(br, bg, bb, BlendLum(fr, fg, fb));
			break;
		}
		default:
			*out_r = ApplyBlend(sr, fr, mode, amount);
			*out_g = ApplyBlend(sg, fg, mode, amount);
			*out_b = ApplyBlend(sb, fb, mode, amount);
			return;
	}
	*out_r = br*amount + sr*(1.0-amount);
	*out_g = bg*amount + sg*(1.0-amount);
	*out_b = bb*amount + sb*(1.0-amount);
}

// -------------------------------------------------------------------
// Per-pixel compose (normalized). xL/yL are LAYER-space coords (for dither).
// -------------------------------------------------------------------

struct OutPix { PF_FpLong r, g, b, a; };

static OutPix Compose(
	PF_FpLong sr, PF_FpLong sg, PF_FpLong sb, PF_FpLong sa,
	PF_FpLong fr, PF_FpLong fg, PF_FpLong fb,
	A_long mode, PF_FpLong amount, A_long xL, A_long yL, A_long seed)
{
	if (mode == BLEND_DITHER || mode == BLEND_DITHER_ONLY) {
		bool hit = Hash01(HashUI((uint32_t)seed, (uint32_t)xL), (uint32_t)yL) < amount;
		if (mode == BLEND_DITHER_ONLY)
			return hit ? OutPix{ fr, fg, fb, sa } : OutPix{ 0.0, 0.0, 0.0, 0.0 };
		return hit ? OutPix{ fr, fg, fb, sa } : OutPix{ sr, sg, sb, sa };
	}
	PF_FpLong or_, og, ob;
	ApplyBlendRGB(sr, sg, sb, fr, fg, fb, mode, amount, &or_, &og, &ob);
	return OutPix{ or_, og, ob, sa };
}

// -------------------------------------------------------------------
// Pixel callbacks
// -------------------------------------------------------------------

static PF_Err
FillFunc8(void *refcon, A_long xL, A_long yL, PF_Pixel8 *inP, PF_Pixel8 *outP)
{
	FillInfoP fiP = reinterpret_cast<FillInfoP>(refcon);
	if (!fiP) return PF_Err_NONE;

	PF_FpLong sr, sg, sb, sa;
	if (fiP->src_world) {
		A_long sx = xL - fiP->src_off_x, sy = yL - fiP->src_off_y;
		if (sx >= 0 && sx < fiP->src_world->width && sy >= 0 && sy < fiP->src_world->height) {
			PF_Pixel8 *p = (PF_Pixel8*)((char*)fiP->src_world->data + sy*fiP->src_world->rowbytes) + sx;
			sr = p->red/255.0; sg = p->green/255.0; sb = p->blue/255.0; sa = p->alpha/255.0;
		} else { sr = sg = sb = sa = 0.0; }
	} else {
		sr = inP->red/255.0; sg = inP->green/255.0; sb = inP->blue/255.0; sa = inP->alpha/255.0;
	}
	PF_FpLong fr = fiP->color.red/255.0, fg = fiP->color.green/255.0, fb = fiP->color.blue/255.0;

	OutPix o = Compose(sr, sg, sb, sa, fr, fg, fb, fiP->blendMode, fiP->amount,
		xL + fiP->out_origin_x, yL + fiP->out_origin_y, fiP->ditherSeed);
	PF_FpLong a = fiP->invertAlpha ? (1.0 - o.a) : o.a;

	outP->red   = (A_u_char)(Clamp01(o.r) * 255.0 + 0.5);
	outP->green = (A_u_char)(Clamp01(o.g) * 255.0 + 0.5);
	outP->blue  = (A_u_char)(Clamp01(o.b) * 255.0 + 0.5);
	outP->alpha = (A_u_char)(Clamp01(a)   * 255.0 + 0.5);
	return PF_Err_NONE;
}

static PF_Err
FillFunc16(void *refcon, A_long xL, A_long yL, PF_Pixel16 *inP, PF_Pixel16 *outP)
{
	FillInfoP fiP = reinterpret_cast<FillInfoP>(refcon);
	if (!fiP) return PF_Err_NONE;

	PF_FpLong max16 = (PF_FpLong)PF_MAX_CHAN16;
	PF_FpLong sr, sg, sb, sa;
	if (fiP->src_world) {
		A_long sx = xL - fiP->src_off_x, sy = yL - fiP->src_off_y;
		if (sx >= 0 && sx < fiP->src_world->width && sy >= 0 && sy < fiP->src_world->height) {
			PF_Pixel16 *p = (PF_Pixel16*)((char*)fiP->src_world->data + sy*fiP->src_world->rowbytes) + sx;
			sr = p->red/max16; sg = p->green/max16; sb = p->blue/max16; sa = p->alpha/max16;
		} else { sr = sg = sb = sa = 0.0; }
	} else {
		sr = inP->red/max16; sg = inP->green/max16; sb = inP->blue/max16; sa = inP->alpha/max16;
	}
	PF_FpLong fr = fiP->color.red/255.0, fg = fiP->color.green/255.0, fb = fiP->color.blue/255.0;

	OutPix o = Compose(sr, sg, sb, sa, fr, fg, fb, fiP->blendMode, fiP->amount,
		xL + fiP->out_origin_x, yL + fiP->out_origin_y, fiP->ditherSeed);
	PF_FpLong a = fiP->invertAlpha ? (1.0 - o.a) : o.a;

	outP->red   = (A_u_short)(Clamp01(o.r) * max16 + 0.5);
	outP->green = (A_u_short)(Clamp01(o.g) * max16 + 0.5);
	outP->blue  = (A_u_short)(Clamp01(o.b) * max16 + 0.5);
	outP->alpha = (A_u_short)(Clamp01(a)   * max16 + 0.5);
	return PF_Err_NONE;
}

static PF_Err
FillFunc32(void *refcon, A_long xL, A_long yL, PF_PixelFloat *inP, PF_PixelFloat *outP)
{
	FillInfoP fiP = reinterpret_cast<FillInfoP>(refcon);
	if (!fiP) return PF_Err_NONE;

	PF_FpLong sr, sg, sb, sa;
	if (fiP->src_world) {
		A_long sx = xL - fiP->src_off_x, sy = yL - fiP->src_off_y;
		if (sx >= 0 && sx < fiP->src_world->width && sy >= 0 && sy < fiP->src_world->height) {
			PF_PixelFloat *p = (PF_PixelFloat*)((char*)fiP->src_world->data + sy*fiP->src_world->rowbytes) + sx;
			sr = p->red; sg = p->green; sb = p->blue; sa = p->alpha;
		} else { sr = sg = sb = sa = 0.0; }
	} else {
		sr = inP->red; sg = inP->green; sb = inP->blue; sa = inP->alpha;
	}
	PF_FpLong fr = fiP->color.red/255.0, fg = fiP->color.green/255.0, fb = fiP->color.blue/255.0;

	OutPix o = Compose(sr, sg, sb, sa, fr, fg, fb, fiP->blendMode, fiP->amount,
		xL + fiP->out_origin_x, yL + fiP->out_origin_y, fiP->ditherSeed);
	PF_FpLong a = fiP->invertAlpha ? (1.0 - o.a) : o.a;

	// 32-bit float: preserve out-of-range (HDR) values, do not clamp rgb.
	outP->red   = (PF_FpShort)o.r;
	outP->green = (PF_FpShort)o.g;
	outP->blue  = (PF_FpShort)o.b;
	outP->alpha = (PF_FpShort)a;
	return PF_Err_NONE;
}

// -------------------------------------------------------------------
// Shared helpers
// -------------------------------------------------------------------

static void BuildFillInfo(PF_ParamDef *params[], FillInfo &fi)
{
	fi.color       = params[FILL_COLOR]->u.cd.value;
	fi.blendMode   = PopupToBlendMode(params[FILL_BLEND_MODE]->u.pd.value);
	fi.amount      = params[FILL_AMOUNT]->u.fs_d.value / 100.0;
	fi.invertAlpha = params[FILL_INVERT_ALPHA]->u.bd.value ? TRUE : FALSE;
	fi.ditherSeed  = params[FILL_DITHER_SEED]->u.sd.value;
}

// Dispatch iterate by the destination world's bit depth (8 / 16 / 32).
static PF_Err IterateFill(PF_InData *in_data, FillInfo &fi, PF_EffectWorld *src, PF_EffectWorld *dst)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	A_long linesL = dst->height;

	PF_PixelFormat fmt = PF_PixelFormat_ARGB32;
	PF_WorldSuite2 *ws2P = NULL;
	in_data->pica_basicP->AcquireSuite(kPFWorldSuite, kPFWorldSuiteVersion2, (const void **)&ws2P);
	if (ws2P) { ws2P->PF_GetPixelFormat(dst, &fmt); in_data->pica_basicP->ReleaseSuite(kPFWorldSuite, kPFWorldSuiteVersion2); }

	if (fmt == PF_PixelFormat_ARGB128)
		ERR(suites.IterateFloatSuite1()->iterate(in_data, 0, linesL, src, NULL, (void*)&fi, FillFunc32, dst));
	else if (fmt == PF_PixelFormat_ARGB64)
		ERR(suites.Iterate16Suite2()->iterate(in_data, 0, linesL, src, NULL, (void*)&fi, FillFunc16, dst));
	else
		ERR(suites.Iterate8Suite2()->iterate(in_data, 0, linesL, src, NULL, (void*)&fi, FillFunc8, dst));
	return err;
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
	out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE;
	out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER       // 8/16/32 bpc
	                     | PF_OutFlag2_FLOAT_COLOR_AWARE
	                     | PF_OutFlag2_SUPPORTS_THREADED_RENDERING; // MFR (render is stateless)
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

	// Blend Mode popup — aligned with KO BandFill (separators count as index slots).
	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Blend Mode",
		POPUP_TOTAL_ITEMS,
		1, // default: Normal
		"Normal"
		"|(-"
		"|Add|Negative Add|Multiply|Screen"
		"|(-"
		"|Overlay|Soft Light|Hard Light|Linear Light"
		"|(-"
		"|Lighten|Darken|Difference"
		"|(-"
		"|Hue|Saturation|Color|Luminosity"
		"|(-"
		"|Dither|Dither Only",
		BLEND_MODE_DISK_ID);

	// Opacity
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Opacity",
		0, 100, 0, 100, 100,
		PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0,
		AMOUNT_DISK_ID);

	// Invert Alpha checkbox
	AEFX_CLR_STRUCT(def);
	def.param_type      = PF_Param_CHECKBOX;
	def.u.bd.value      = FALSE;
	def.u.bd.dephault   = FALSE;
	def.uu.id           = INVERT_ALPHA_DISK_ID;
	PF_STRNNCPY(def.PF_DEF_NAME, "Invert Alpha", sizeof(def.PF_DEF_NAME));
	ERR(PF_ADD_PARAM(in_data, -1, &def));

	// Dither Seed (used by Dither / Dither Only modes)
	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Dither Seed",
		0, 10000, 0, 1000, 0,
		DITHER_SEED_DISK_ID);

	out_data->num_params = FILL_NUM_PARAMS;
	return err;
}

// Legacy render path (no mask/composite-options; used by hosts that don't drive SmartRender).
static PF_Err
Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err err = PF_Err_NONE;

	FillInfo fi;
	AEFX_CLR_STRUCT(fi);
	BuildFillInfo(params, fi); // src_world stays NULL → FillFunc samples inP

	ERR(IterateFill(in_data, fi, &params[FILL_INPUT]->u.ld, output));
	return err;
}

static PF_Err
SmartPreRender(PF_InData *in_data, PF_OutData *out_data, PF_PreRenderExtra *extra)
{
	A_long ds_num = in_data->downsample_x.num, ds_den = (A_long)in_data->downsample_x.den;
	if (ds_den < 1) ds_den = 1;
	A_long ds_w = (A_long)((PF_FpLong)in_data->width  * ds_num / ds_den + 0.5);
	A_long ds_h = (A_long)((PF_FpLong)in_data->height * in_data->downsample_y.num / (A_long)in_data->downsample_y.den + 0.5);
	if (ds_w < 1) ds_w = 1;
	if (ds_h < 1) ds_h = 1;

	const PF_Rect &req = extra->input->output_request.rect;
	extra->output->result_rect.left   = MAX(req.left,   0L);
	extra->output->result_rect.top    = MAX(req.top,    0L);
	extra->output->result_rect.right  = MIN(req.right,  ds_w);
	extra->output->result_rect.bottom = MIN(req.bottom, ds_h);
	extra->output->max_result_rect    = { 0, 0, ds_w, ds_h };
	extra->output->solid              = FALSE;

	// Declare dependency on the source layer so checkout_layer_pixels works in
	// SmartRender. Save the checked-out rect (= src_worldP layer-space origin) so
	// SmartRender can offset-sample it correctly under a mask bounding box.
	PF_RenderRequest src_req = extra->input->output_request;
	src_req.rect             = extra->output->result_rect;
	A_long time_step = in_data->time_step > 0 ? in_data->time_step : 1;
	PF_CheckoutResult src_result = {};
	extra->cb->checkout_layer(in_data->effect_ref,
		FILL_INPUT, FILL_INPUT,
		&src_req,
		in_data->current_time, time_step, in_data->time_scale,
		&src_result);

	PF_Rect *rd = new(std::nothrow) PF_Rect(src_result.result_rect);
	if (rd) {
		extra->output->pre_render_data = rd;
		extra->output->delete_pre_render_data_func = [](void *p){ delete static_cast<PF_Rect*>(p); };
	}
	return PF_Err_NONE;
}

static PF_Err
SmartRender(PF_InData *in_data, PF_OutData *out_data, PF_SmartRenderExtra *extra)
{
	PF_Err err = PF_Err_NONE;

	// checkout_layer_pixels must precede checkout_output (AE requirement).
	PF_EffectWorld *src_worldP = nullptr;
	PF_Err src_err = extra->cb->checkout_layer_pixels(in_data->effect_ref, FILL_INPUT, &src_worldP);

	PF_EffectWorld *output_worldP = NULL;
	ERR(extra->cb->checkout_output(in_data->effect_ref, &output_worldP));
	if (err || !output_worldP) {
		if (src_err == PF_Err_NONE) extra->cb->checkin_layer_pixels(in_data->effect_ref, FILL_INPUT);
		return err;
	}

	A_long time_step = in_data->time_step > 0 ? in_data->time_step : 1;
	PF_ParamDef  param_storage[FILL_NUM_PARAMS] = {};
	PF_ParamDef *params_ptrs[FILL_NUM_PARAMS]   = {};
	for (int i = FILL_COLOR; i < FILL_NUM_PARAMS; i++) {
		if (PF_CHECKOUT_PARAM(in_data, i, in_data->current_time, time_step, in_data->time_scale, &param_storage[i]) == PF_Err_NONE)
			params_ptrs[i] = &param_storage[i];
	}

	FillInfo fi;
	AEFX_CLR_STRUCT(fi);
	BuildFillInfo(params_ptrs, fi);

	// Output world's layer-space origin O — non-zero when AE clips the output to a
	// compositing-options mask bbox. iterate then passes (xL,yL) as 0-based coords in
	// that sub-rect, so layer_x = O + xL. in_data->output_origin does NOT capture this.
	A_long o_x = MAX(extra->input->output_request.rect.left, 0L);
	A_long o_y = MAX(extra->input->output_request.rect.top,  0L);
	fi.out_origin_x = o_x;
	fi.out_origin_y = o_y;

	// Masked source: sample src_worldP directly in FillFunc.
	// src_worldP origin in layer space = result_rect saved by SmartPreRender.
	// FillFunc maps output (xL,yL) → src ((xL+O) - src_origin) = xL - src_off.
	fi.src_world = src_worldP;
	const PF_Rect *rd = static_cast<const PF_Rect*>(extra->input->pre_render_data);
	fi.src_off_x = (rd ? rd->left : 0) - o_x;
	fi.src_off_y = (rd ? rd->top  : 0) - o_y;

	for (int i = FILL_COLOR; i < FILL_NUM_PARAMS; i++) {
		if (params_ptrs[i]) PF_CHECKIN_PARAM(in_data, &param_storage[i]);
	}

	ERR(IterateFill(in_data, fi, output_worldP, output_worldP));

	if (src_err == PF_Err_NONE) extra->cb->checkin_layer_pixels(in_data->effect_ref, FILL_INPUT);
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
		"https://github.com/yama-ko/KO-SimpleFill");
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
			case PF_Cmd_ABOUT:            err = About(in_data, out_data, params, output);       break;
			case PF_Cmd_GLOBAL_SETUP:     err = GlobalSetup(in_data, out_data, params, output); break;
			case PF_Cmd_PARAMS_SETUP:     err = ParamsSetup(in_data, out_data, params, output); break;
			case PF_Cmd_RENDER:           err = Render(in_data, out_data, params, output);       break;
			case PF_Cmd_SMART_PRE_RENDER: err = SmartPreRender(in_data, out_data, (PF_PreRenderExtra*)extra);  break;
			case PF_Cmd_SMART_RENDER:     err = SmartRender(in_data, out_data, (PF_SmartRenderExtra*)extra);   break;
		}
	}
	catch (PF_Err &thrown_err) { err = thrown_err; }
	return err;
}
