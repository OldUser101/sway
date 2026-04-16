{
  lib,
  stdenv,
  fetchFromGitHub,
  replaceVars,
  swaybg,
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
  wlroots_0_19,
  wayland-protocols,
  libdrm,
  libxcb-wm,
  systemd,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "sway";
  version = "1.11";

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
    wlroots_0_19
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
