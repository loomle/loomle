const path = require("node:path");
const { pathToFileURL } = require("node:url");
const { chromium } = require("playwright");

async function main() {
  const args = process.argv.slice(2);
  if (args.length === 0 || args.length % 2 !== 0) {
    throw new Error(
      "Usage: node render-media.cjs <input.html> <output.png> [<input.html> <output.png> ...]",
    );
  }

  const executablePath =
    process.env.LOOMLE_CHROME_PATH ||
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
  const browser = await chromium.launch({
    executablePath,
    headless: true,
  });
  for (let index = 0; index < args.length; index += 2) {
    const input = path.resolve(args[index]);
    const output = path.resolve(args[index + 1]);
    const page = await browser.newPage({
      viewport: { width: 1920, height: 1080 },
      deviceScaleFactor: 1,
    });

    await page.goto(pathToFileURL(input).href, { waitUntil: "networkidle" });
    await page.screenshot({
      path: output,
      type: "png",
      fullPage: false,
    });
    await page.close();
  }
  await browser.close();
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
