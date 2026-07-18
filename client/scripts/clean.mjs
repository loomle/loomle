import { rmSync } from "node:fs";

rmSync(new URL("../dist", import.meta.url), { recursive: true, force: true });
rmSync(new URL("../../.tmp/client-tests", import.meta.url), { recursive: true, force: true });
