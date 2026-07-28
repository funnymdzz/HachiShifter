#!/usr/bin/env node

// Quality gate for the FCPE report produced by --compare-vocal-f0.
// Keep the defaults strict enough to catch the pitch/timing and boundary
// regressions seen in imported MPD projects while allowing callers to tighten
// them for a particular reference vocal through environment variables.

const fs = require("node:fs");

if (process.argv.length !== 3) {
  console.error("usage: assert_nsf_hifigan_reference.js REPORT.json");
  process.exit(2);
}

const report = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const numberFromEnv = (name, fallback) => {
  const raw = process.env[name];
  if (raw === undefined || raw.trim() === "") return fallback;
  const value = Number(raw);
  if (!Number.isFinite(value)) {
    throw new Error(`${name} must be a finite number`);
  }
  return value;
};

const limits = {
  maxMedianCents: numberFromEnv("HACHISHIFTER_NSF_MAX_MEDIAN_CENTS", 25),
  maxP90Cents: numberFromEnv("HACHISHIFTER_NSF_MAX_P90_CENTS", 180),
  minEnvelopeCorrelation: numberFromEnv(
    "HACHISHIFTER_NSF_MIN_ENVELOPE_CORRELATION",
    0.8,
  ),
  minPairedVoicedRatio: numberFromEnv(
    "HACHISHIFTER_NSF_MIN_PAIRED_VOICED_RATIO",
    0.85,
  ),
  maxStepExcess: numberFromEnv("HACHISHIFTER_NSF_MAX_STEP_EXCESS", 0.1),
  maxLargeStepRatio: numberFromEnv(
    "HACHISHIFTER_NSF_MAX_LARGE_STEP_RATIO",
    1.25,
  ),
};

const pairedVoicedRatio =
  Number(report.reference_voiced_frames) > 0
    ? Number(report.paired_voiced_frames) / Number(report.reference_voiced_frames)
    : 0;
const maxAllowedStep =
  Number(report.reference_max_sample_step) + limits.maxStepExcess;
const maxAllowedLargeSteps =
  Number(report.reference_steps_over_0_20) * limits.maxLargeStepRatio + 32;

const failures = [];
const checkMax = (label, value, limit) => {
  if (!Number.isFinite(value) || value > limit) {
    failures.push(`${label}=${value} > ${limit}`);
  }
};
const checkMin = (label, value, limit) => {
  if (!Number.isFinite(value) || value < limit) {
    failures.push(`${label}=${value} < ${limit}`);
  }
};

checkMax("median_abs_cents", Number(report.median_abs_cents), limits.maxMedianCents);
checkMax("p90_abs_cents", Number(report.p90_abs_cents), limits.maxP90Cents);
checkMin(
  "envelope_correlation",
  Number(report.envelope_correlation),
  limits.minEnvelopeCorrelation,
);
checkMin("paired_voiced_ratio", pairedVoicedRatio, limits.minPairedVoicedRatio);
checkMax(
  "rendered_max_sample_step",
  Number(report.rendered_max_sample_step),
  maxAllowedStep,
);
checkMax(
  "rendered_steps_over_0_20",
  Number(report.rendered_steps_over_0_20),
  maxAllowedLargeSteps,
);

if (failures.length > 0) {
  console.error("NSF-HiFiGAN reference gate failed:");
  for (const failure of failures) console.error(`- ${failure}`);
  process.exit(1);
}

console.log(
  [
    "NSF-HiFiGAN reference gate passed",
    `median=${Number(report.median_abs_cents).toFixed(3)} cents`,
    `p90=${Number(report.p90_abs_cents).toFixed(3)} cents`,
    `paired=${pairedVoicedRatio.toFixed(4)}`,
    `maxStep=${Number(report.rendered_max_sample_step).toFixed(6)}`,
    `largeSteps=${Number(report.rendered_steps_over_0_20)}`,
  ].join("; "),
);
