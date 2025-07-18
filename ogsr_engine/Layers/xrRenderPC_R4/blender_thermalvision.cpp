#include "stdafx.h"

#include "blender_thermalvision.h"

CBlender_heatvision::CBlender_heatvision() { description.CLS = 0; } //--DSR-- Heatvision

CBlender_heatvision::~CBlender_heatvision() {}

void CBlender_heatvision::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

    switch (C.iElement)
    {
    case 0: // Dummy shader - because IDK what gonna happen when r2_nightvision will be 0
        C.r_Pass("stub_screen_space", "copy_nomsaa", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_generic", r2_RT_generic0);

        C.r_dx10Sampler("smp_base");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    case 1:
        C.r_Pass("stub_screen_space", "heatvision", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_image", r2_RT_generic0);
        C.r_dx10Texture("s_bloom_new", r2_RT_pp_bloom);
        C.r_dx10Texture("s_blur_2", r2_RT_blur_2);
        C.r_dx10Texture("s_blur_4", r2_RT_blur_4);
        C.r_dx10Texture("s_blur_8", r2_RT_blur_8);

        C.r_dx10Texture("s_heat", r2_RT_heat); //--DSR-- HeatVision

        C.r_dx10Sampler("smp_base");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    }
}
