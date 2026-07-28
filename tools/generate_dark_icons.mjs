#!/usr/bin/env node

import { mkdir, readdir, readFile, writeFile } from "node:fs/promises";
import { basename, dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const toolsDirectory = dirname(fileURLToPath(import.meta.url));
const projectDirectory = dirname(toolsDirectory);
const sourceDirectory = join(projectDirectory, "assets", "icons", "ui");
const outputDirectory = join(projectDirectory, "assets", "icons", "ui-dark");

await mkdir(outputDirectory, { recursive: true });

const iconFiles = (await readdir(sourceDirectory))
    .filter(fileName => fileName.endsWith(".svg"))
    .sort();

for (const fileName of iconFiles) {
    const sourcePath = join(sourceDirectory, fileName);
    const outputPath = join(outputDirectory, basename(fileName));
    let svg = await readFile(sourcePath, "utf8");

    // QML Image does not propagate a control's color into SVG currentColor.
    // Bake in a high-contrast dark-workbench stroke and add a small optical
    // weight adjustment so 16–20 px toolbar icons remain crisp.
    svg = svg
        .replaceAll('stroke="currentColor"', 'stroke="#E8E8E8"')
        .replaceAll('stroke-width="2"', 'stroke-width="2.15"');

    // Keep platform folder colors recognizable but lift the darkest portions
    // that otherwise disappear against the VS Code-style dark surfaces.
    svg = svg
        .replaceAll("#359BEA", "#4BAAF2")
        .replaceAll("#48A9F8", "#55B2FA")
        .replaceAll("#E8A317", "#F0AD24")
        .replaceAll("#7D8A94", "#A6AFB5")
        .replaceAll("#D7DDE2", "#DDE2E5");

    await writeFile(outputPath, svg);
}

console.log(`Generated ${iconFiles.length} dark icons in ${outputDirectory}`);
