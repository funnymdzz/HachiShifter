//! 基于 WORLD 声码器的渲染器实现。

use super::traits::{RenderContext, Renderer, RendererCapabilities};
use super::utils::{clip_midi_at_time, edit_midi_at_time_or_none};
use crate::state::SynthPipelineKind;

/// 基于 WORLD 声码器的渲染器。
pub struct WorldRenderer;

impl Renderer for WorldRenderer {
    fn id(&self) -> &str {
        "world_vocoder"
    }

    fn display_name(&self) -> &str {
        "WORLD Vocoder"
    }

    fn kind(&self) -> SynthPipelineKind {
        SynthPipelineKind::WorldVocoder
    }

    fn is_available(&self) -> bool {
        crate::world_vocoder::is_available()
    }

    fn render(&self, ctx: &RenderContext<'_>) -> Result<Vec<f32>, String> {
        render_world(ctx, false)
    }

    fn capabilities(&self) -> RendererCapabilities {
        RendererCapabilities {
            supports_realtime: true,
            prefers_prerender: false,
            max_pitch_shift_semitones: 24.0,
        }
    }
}

pub(crate) fn render_mld5(ctx: &RenderContext<'_>) -> Result<Vec<f32>, String> {
    render_world(ctx, true)
}

fn render_world(ctx: &RenderContext<'_>, mld5_components: bool) -> Result<Vec<f32>, String> {
    let f0_floor = 40.0;
    let f0_ceil = 1600.0;
    let fp = ctx.frame_period_ms;
    let clip_start = ctx.clip_start_sec;
    let pitch_edit = ctx.pitch_edit;
    let clip_midi = ctx.clip_midi;

    let target = move |abs_time_sec: f64, detected_f0_hz: f64| {
        let orig = clip_midi_at_time(fp, clip_start, clip_midi, abs_time_sec);
        if !(orig.is_finite() && orig > 0.0) {
            return detected_f0_hz;
        }
        let target = match edit_midi_at_time_or_none(fp, pitch_edit, abs_time_sec) {
            Some(v) => v,
            None => orig,
        };
        let target = orig + (target - orig).clamp(-24.0, 24.0);
        let hz = 440.0 * 2.0f64.powf((target - 69.0) / 12.0);
        if hz.is_finite() && hz > 0.0 {
            hz
        } else {
            detected_f0_hz
        }
    };
    if mld5_components {
        crate::world_vocoder::vocode_pitch_shift_chunked_mld5(
            ctx.mono_pcm,
            ctx.sample_rate,
            ctx.seg_start_sec,
            fp,
            f0_floor,
            f0_ceil,
            target,
        )
    } else {
        crate::world_vocoder::vocode_pitch_shift_chunked(
            ctx.mono_pcm,
            ctx.sample_rate,
            ctx.seg_start_sec,
            fp,
            f0_floor,
            f0_ceil,
            target,
        )
    }
}
