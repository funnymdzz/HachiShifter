function escapeHtml(text: string): string {
    return text
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#39;");
}

function renderInlineMarkdown(text: string): string {
    const escaped = escapeHtml(text);

    const withLinks = escaped.replace(
        /\[([^\]]+)\]\((https?:\/\/[^\s)]+)\)/g,
        (_match, label: string, href: string) =>
            `<a href="${href}" target="_blank" rel="noreferrer">${label}</a>`,
    );

    return withLinks.replace(/`([^`]+)`/g, "<code>$1</code>");
}

function flushParagraph(lines: string[], blocks: string[]): void {
    if (lines.length === 0) return;
    blocks.push(`<p>${renderInlineMarkdown(lines.join(" "))}</p>`);
    lines.length = 0;
}

function flushList(items: string[], blocks: string[]): void {
    if (items.length === 0) return;
    blocks.push(`<ul>${items.map((item) => `<li>${renderInlineMarkdown(item)}</li>`).join("")}</ul>`);
    items.length = 0;
}

export function renderMarkdownPreview(markdown: string): string {
    const lines = markdown.replaceAll("\r\n", "\n").split("\n");
    const blocks: string[] = [];
    const paragraphLines: string[] = [];
    const listItems: string[] = [];
    let inCodeFence = false;
    let codeFenceLang = "";
    let codeFenceLines: string[] = [];

    const flushCodeFence = () => {
        blocks.push(
            `<pre><code${codeFenceLang ? ` class="language-${escapeHtml(codeFenceLang)}"` : ""}>${escapeHtml(
                codeFenceLines.join("\n"),
            )}</code></pre>`,
        );
        codeFenceLines = [];
        codeFenceLang = "";
    };

    for (const rawLine of lines) {
        const line = rawLine.trimEnd();

        if (inCodeFence) {
            if (line.startsWith("```")) {
                flushCodeFence();
                inCodeFence = false;
            } else {
                codeFenceLines.push(rawLine);
            }
            continue;
        }

        if (line.startsWith("```")) {
            flushParagraph(paragraphLines, blocks);
            flushList(listItems, blocks);
            inCodeFence = true;
            codeFenceLang = line.slice(3).trim();
            continue;
        }

        if (!line.trim()) {
            flushParagraph(paragraphLines, blocks);
            flushList(listItems, blocks);
            continue;
        }

        if (/^(-{3,}|\*{3,})$/.test(line)) {
            flushParagraph(paragraphLines, blocks);
            flushList(listItems, blocks);
            blocks.push("<hr />");
            continue;
        }

        const heading = line.match(/^(#{1,6})\s+(.*)$/);
        if (heading) {
            flushParagraph(paragraphLines, blocks);
            flushList(listItems, blocks);
            const level = heading[1].length;
            blocks.push(`<h${level}>${renderInlineMarkdown(heading[2])}</h${level}>`);
            continue;
        }

        const quote = line.match(/^>\s?(.*)$/);
        if (quote) {
            flushParagraph(paragraphLines, blocks);
            flushList(listItems, blocks);
            blocks.push(`<blockquote>${renderInlineMarkdown(quote[1])}</blockquote>`);
            continue;
        }

        const listItem = line.match(/^[-*]\s+(.*)$/);
        if (listItem) {
            flushParagraph(paragraphLines, blocks);
            listItems.push(listItem[1]);
            continue;
        }

        flushList(listItems, blocks);
        paragraphLines.push(line.trim());
    }

    if (inCodeFence) {
        flushCodeFence();
    }
    flushParagraph(paragraphLines, blocks);
    flushList(listItems, blocks);

    return blocks.join("");
}
