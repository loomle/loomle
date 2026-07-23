import { readFile, readdir, stat } from "node:fs/promises";
import { join } from "node:path";

const LICENSE_FILE_PATTERN = /^(LICENSE|LICENCE|COPYING|NOTICE)([-_.]|$)/i;

export async function renderThirdPartyNotices({
  repoRoot,
  nodeLicensePath,
  nodeVersion,
}) {
  const nodeLicense = await readRequiredText(nodeLicensePath, "Node.js license");
  const lock = JSON.parse(await readFile(join(repoRoot, "package-lock.json"), "utf8"));
  const packages = [];

  for (const [packagePath, lockEntry] of Object.entries(lock.packages ?? {})) {
    if (!packagePath.startsWith("node_modules/")
        || lockEntry.dev === true
        || lockEntry.link === true) {
      continue;
    }

    const packageDirectory = join(repoRoot, packagePath);
    const manifest = JSON.parse(await readFile(join(packageDirectory, "package.json"), "utf8"));
    if (typeof manifest.name !== "string" || typeof manifest.version !== "string") {
      throw new Error(`production dependency has no name/version: ${packagePath}`);
    }

    const licenseFiles = (await readdir(packageDirectory))
      .filter((name) => LICENSE_FILE_PATTERN.test(name))
      .sort();
    if (licenseFiles.length === 0) {
      throw new Error(
        `production dependency ${manifest.name}@${manifest.version}`
        + ` has no distributable license file: ${packagePath}`,
      );
    }

    packages.push({
      key: `${manifest.name}@${manifest.version}`,
      name: manifest.name,
      version: manifest.version,
      declaredLicense: normalizeDeclaredLicense(manifest.license),
      licenseTexts: await Promise.all(licenseFiles.map(async (name) => ({
        name,
        text: await readRequiredText(join(packageDirectory, name), `${manifest.name} ${name}`),
      }))),
    });
  }

  packages.sort((left, right) => (left.key < right.key ? -1 : left.key > right.key ? 1 : 0));
  const uniquePackages = [];
  for (const entry of packages) {
    const previous = uniquePackages.at(-1);
    if (previous?.key !== entry.key) {
      uniquePackages.push(entry);
      continue;
    }
    if (JSON.stringify(previous) !== JSON.stringify(entry)) {
      throw new Error(`conflicting license texts found for ${entry.key}`);
    }
  }

  const sections = [
    [
      "LOOMLE THIRD-PARTY NOTICES",
      "",
      "This file contains license texts for the runtime and production",
      "dependencies distributed inside the standalone Loomle Client.",
    ].join("\n"),
    renderSection(`Node.js ${nodeVersion}`, undefined, [
      { name: "LICENSE", text: nodeLicense },
    ]),
    ...uniquePackages.map((entry) => renderSection(
      `${entry.name} ${entry.version}`,
      entry.declaredLicense,
      entry.licenseTexts,
    )),
  ];

  return `${sections.join("\n\n")}\n`;
}

function renderSection(title, declaredLicense, licenseTexts) {
  const heading = `================================================================================\n${title}`;
  const declaration = declaredLicense ? `\nDeclared license: ${declaredLicense}` : "";
  const texts = licenseTexts.map(({ name, text }) => (
    `\n--------------------------------------------------------------------------------\n`
    + `${name}\n`
    + `--------------------------------------------------------------------------------\n`
    + text
  )).join("\n");
  return `${heading}${declaration}${texts}`;
}

function normalizeDeclaredLicense(value) {
  if (typeof value === "string") return value;
  if (Array.isArray(value)) return value.map(normalizeDeclaredLicense).filter(Boolean).join(", ");
  if (value && typeof value.type === "string") return value.type;
  return undefined;
}

async function readRequiredText(path, label) {
  let fileStat;
  try {
    fileStat = await stat(path);
  } catch (error) {
    if (error?.code === "ENOENT") throw new Error(`${label} not found: ${path}`);
    throw error;
  }
  if (!fileStat.isFile() || fileStat.size === 0) {
    throw new Error(`${label} must be a non-empty file: ${path}`);
  }
  return (await readFile(path, "utf8")).replace(/\r\n/g, "\n").trimEnd();
}
