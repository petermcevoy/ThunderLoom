#pragma once
#include <stdint.h>
#include "wif/wif.h"

typedef struct
{
// These are the parameters to the model
    float uscale;
    float vscale;
    float umax;
    float psi;
    float alpha;
    float beta;
    float delta_x;
    float specular_strength; //TODO(Vidar): Does this really belong here?
    float intensity_fineness;
    float yarnvar_amplitude;
    float yarnvar_xscale;
    float yarnvar_yscale;
    float yarnvar_persistance;
    uint32_t yarnvar_octaves;

// These are set by calling one of the wcWeavePatternFrom* functions
// after all parameters above have been defined
    uint32_t pattern_height;
    uint32_t pattern_width;
    PaletteEntry * pattern_entry;
    float specular_normalization;
} wcWeaveParameters;

typedef struct
{
    float color_r, color_g, color_b;
    float normal_x, normal_y, normal_z; //(not yarn local coordinates)
    float u, v; //Segment uv coordinates (in angles)
    float length, width; //Segment length and width
    float x, y; //position within segment (in yarn local coordiantes). 
    uint32_t total_index_x, total_index_y; //index for elements (not yarn local coordinates). TODO(Peter): perhaps a better name would be good?
    uint8_t warp_above; 
} wcPatternData;

typedef struct
{
    float uv_x, uv_y;
    float wi_x, wi_y, wi_z;
    float wo_x, wo_y, wo_z;
} wcIntersectionData;

void wcWeavePatternFromData(wcWeaveParameters *params, uint8_t *warp_above,
    float *warp_color, float *weft_color, uint32_t pattern_width,
    uint32_t pattern_height);
void wcWeavePatternFromFile(wcWeaveParameters *params, const char *filename);
void wcWeavePatternFromFile_wchar(wcWeaveParameters *params,
    const wchar_t *filename);
/* wcWeavePatternFromFile calles one of the functions below depending on
 * file extension*/
void wcWeavePatternFromWIF(wcWeaveParameters *params, const char *filename);
void wcWeavePatternFromWIF_wchar(wcWeaveParameters *params,
    const wchar_t *filename);
void wcWeavePatternFromWeaveFile(wcWeaveParameters *params, const char *filename);
void wcWeavePatternFromWeaveFile_wchar(wcWeaveParameters *params,
    const wchar_t *filename);



void wcFreeWeavePattern(wcWeaveParameters *params);

wcPatternData wcGetPatternData(wcIntersectionData intersection_data,
    const wcWeaveParameters *params);

float wcEvalDiffuse(wcIntersectionData intersection_data,
    wcPatternData data, const wcWeaveParameters *params);

float wcEvalSpecular(wcIntersectionData intersection_data,
    wcPatternData data, const wcWeaveParameters *params);
