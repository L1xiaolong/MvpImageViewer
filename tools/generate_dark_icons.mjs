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

    // Runtime SVGs contain explicit colors: QML Image does not inherit a
    // control's foreground. Keep approved geometry and stroke weights intact.
    svg = svg
        .replaceAll("#303036", "#DDDDE2")
        .replaceAll("#A9771E", "#D7B06B")
        .replaceAll("#3D7FAD", "#7AB5DF");

    await writeFile(outputPath, svg);
}

console.log(`Generated ${iconFiles.length} dark icons in ${outputDirectory}`);
