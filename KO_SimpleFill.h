#pragma once

#ifndef SIMPLEFILL_H
#define SIMPLEFILL_H

typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned short u_int16;
typedef unsigned long  u_long;
typedef short int      int16;
#define PF_TABLE_BITS  12
#define PF_TABLE_SZ_16 4096

#define PF_DEEP_COLOR_AWARE 1

#include "AEConfig.h"

#ifdef AE_OS_WIN
	typedef unsigned short PixelType;
	#include <Windows.h>
#endif

#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "AE_GeneralPlug.h"
#include "AEFX_ChannelDepthTpl.h"
#include "AEGP_SuiteHandler.h"

#define MAJOR_VERSION 2
#define MINOR_VERSION 0
#define BUG_VERSION   0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 1

// Blend Mode popup — aligned with KO BandFill (minus None / Stencil / Silhouette).
// 1-based; separators occupy index slots.
//  Normal(1) | sep(2) | Add(3) NegAdd(4) Multiply(5) Screen(6)
//  | sep(7) | Overlay(8) SoftLight(9) HardLight(10) LinearLight(11)
//  | sep(12) | Lighten(13) Darken(14) Difference(15)
//  | sep(16) | Hue(17) Saturation(18) Color(19) Luminosity(20)
//  | sep(21) | Dither(22) Dither Only(23)
#define POPUP_TOTAL_ITEMS 23

enum {
	FILL_INPUT = 0,
	FILL_COLOR,
	FILL_BLEND_MODE,
	FILL_AMOUNT,
	FILL_INVERT_ALPHA,
	FILL_DITHER_SEED,
	FILL_NUM_PARAMS
};

enum {
	COLOR_DISK_ID = 1,
	BLEND_MODE_DISK_ID,
	AMOUNT_DISK_ID,
	INVERT_ALPHA_DISK_ID,
	DITHER_SEED_DISK_ID,
};

// Internal blend mode IDs (mapped from popup value in Render)
enum BlendMode {
	BLEND_NORMAL = 0,
	BLEND_ADD,           // scale: src + fill * amount
	BLEND_NEGATIVE_ADD,  // scale: src - (1-fill) * amount
	BLEND_MULTIPLY,      // lerp: src * fill
	BLEND_SCREEN,        // lerp: 1-(1-src)(1-fill)
	BLEND_OVERLAY,       // lerp
	BLEND_HARD_LIGHT,    // lerp
	BLEND_LIGHTEN,       // lerp: max(src, fill)
	BLEND_DARKEN,        // lerp: min(src, fill)
	BLEND_DIFFERENCE,    // lerp: |src - fill|
	BLEND_SOFT_LIGHT,    // lerp (W3C/AE)
	BLEND_LINEAR_LIGHT,  // lerp: clamp(s + 2f - 1)
	BLEND_HUE,           // non-separable
	BLEND_SATURATION,    // non-separable
	BLEND_COLOR,         // non-separable
	BLEND_LUMINOSITY,    // non-separable
	BLEND_DITHER,        // probabilistic: hit -> fill, miss -> src
	BLEND_DITHER_ONLY,   // probabilistic: hit -> fill, miss -> transparent
};

typedef struct FillInfo {
	PF_Pixel8  color;
	A_long     blendMode;   // BlendMode enum
	PF_FpLong  amount;      // 0.0 - 1.0
	PF_Boolean invertAlpha;
	A_long     ditherSeed;  // Dither / Dither Only

	// SmartRender only (legacy Render leaves these zeroed → sample inP directly).
	PF_EffectWorld *src_world;                 // masked source; NULL → use inP
	A_long          src_off_x, src_off_y;      // src_world layer-space origin, minus output origin O
	A_long          out_origin_x, out_origin_y; // output world origin O (composite-options mask bbox)
} FillInfo, *FillInfoP, **FillInfoH;

extern "C" {
	DllExport
	PF_Err EffectMain(
		PF_Cmd       cmd,
		PF_InData   *in_data,
		PF_OutData  *out_data,
		PF_ParamDef *params[],
		PF_LayerDef *output,
		void        *extra);
}

#endif // SIMPLEFILL_H
