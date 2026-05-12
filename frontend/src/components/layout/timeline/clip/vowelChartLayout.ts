export interface VowelPoint {
    label: string;
    f1: number;
    f2: number;
}

export const VOWEL_POINTS: VowelPoint[] = [
    { label: "i", f1: 260, f2: 2400 },
    { label: "y", f1: 260, f2: 2000 },
    { label: "ɨ", f1: 280, f2: 1650 },
    { label: "ʉ", f1: 280, f2: 1450 },
    { label: "ɯ", f1: 260, f2: 1300 },
    { label: "u", f1: 260, f2: 700 },
    { label: "ɪ", f1: 350, f2: 2100 },
    { label: "ʏ", f1: 350, f2: 1700 },
    { label: "ʊ", f1: 350, f2: 1000 },
    { label: "e", f1: 400, f2: 2250 },
    { label: "ø", f1: 400, f2: 1850 },
    { label: "ɘ", f1: 430, f2: 1600 },
    { label: "ɵ", f1: 430, f2: 1400 },
    { label: "ɤ", f1: 400, f2: 1100 },
    { label: "o", f1: 400, f2: 800 },
    { label: "ə", f1: 500, f2: 1400 },
    { label: "ɛ", f1: 600, f2: 1950 },
    { label: "œ", f1: 600, f2: 1600 },
    { label: "ɜ", f1: 600, f2: 1400 },
    { label: "ɞ", f1: 600, f2: 1200 },
    { label: "ʌ", f1: 600, f2: 1100 },
    { label: "ɔ", f1: 600, f2: 850 },
    { label: "æ", f1: 760, f2: 1650 },
    { label: "ɐ", f1: 770, f2: 1350 },
    { label: "a", f1: 850, f2: 1400 },
    { label: "ɶ", f1: 850, f2: 1300 },
    { label: "ɑ", f1: 850, f2: 1050 },
    { label: "ɒ", f1: 850, f2: 800 },
];

export const VOWEL_GUIDE_LINES: string[][] = [
    ["i", "e", "ɛ", "æ", "a"],
    ["y", "ø", "œ"],
    ["ɯ", "ɤ", "ʌ", "ɑ"],
    ["u", "o", "ɔ", "ɒ"],
];
