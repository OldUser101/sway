{
  lib,
  stdenv,
  meson,
  ninja,
  pkg-config,
  wayland-scanner,
  scdoc,
  libGL,
  wayland,
  libxkbcommon,
  pcre2,
  json_c,
  libevdev,
  pango,
  cairo,
  libinput,
  gdk-pixbuf,
  librsvg,
  wlroots_0_20,
  wayland-protocols,
  libdrm,
  libxcb-wm,
  version ? "git",
}:

stdenv.mkDerivation (finalAttrs: {
  inherit version;
  pname = "sway-unwrapped";

  src = ./.;

  strictDeps = true;
  depsBuildBuild = [
    pkg-config
  ];

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    wayland-scanner
    scdoc
  ];

  buildInputs = [
    libGL
    wayland
    libxkbcommon
    pcre2
    json_c
    libevdev
    pango
    cairo
    libinput
    gdk-pixbuf
    librsvg
    wayland-protocols
    libdrm
    libxcb-wm
    wlroots_0_20
  ];

  mesonFlags =
    let
      inherit (lib.strings) mesonEnable mesonOption;
    in
    [
      (mesonOption "sd-bus-provider" "libsystemd")
      (mesonEnable "tray" true)
    ];

  meta = {
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "sway";
  };
})
