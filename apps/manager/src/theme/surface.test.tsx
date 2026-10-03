import { render } from "@testing-library/react";
import { expect, it } from "vitest";

import { PortalSurface, SurfaceProvider } from "./surface";

it("gives portal content the palette tokens without the app shell box", () => {
  const { container } = render(
    <SurfaceProvider value={{ palette: "ink" }}><PortalSurface><p>Dialog</p></PortalSurface></SurfaceProvider>,
  );
  const surface = container.querySelector(".portal-surface");
  expect(surface).toHaveStyle("--lamp: #5fd0e8");
  expect(surface).toHaveAttribute("data-palette", "ink");
  // .app-shell is a full-height flex box; on a portal wrapper it adds a block to body.
  expect(surface).not.toHaveClass("app-shell");
});
