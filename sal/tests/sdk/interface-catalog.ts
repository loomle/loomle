import type { SalInterface } from "../../src/index.js";

export const testInterfaceCatalog = [
  {
    name: "asset",
    description: "Test Asset interface.",
    text: "# Test Asset Interface\n",
  },
  {
    name: "blueprint",
    description: "Test Blueprint interface.",
    text: "# Test Blueprint Interface\n",
  },
  {
    name: "graph",
    description: "Test Graph interface.",
    text: "# Test Graph Interface\n",
  },
  {
    name: "widget",
    description: "Test Widget interface.",
    text: "# Test Widget Interface\n",
  },
] satisfies readonly SalInterface[];
