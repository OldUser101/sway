{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { self, nixpkgs }:
    let
      forAllSystems =
        f:
        nixpkgs.lib.genAttrs nixpkgs.lib.systems.flakeExposed (system: f nixpkgs.legacyPackages.${system});

      shortRev = self.shortRev or self.dirtyShortRev or "unknown";
      overlays = import ./overlay.nix { inherit shortRev; };
    in
    {
      inherit overlays;

      packages = forAllSystems (pkgs: {
        sway = pkgs.callPackage ./package.nix { version = "olduser101-git-${shortRev}"; };
        default = self.packages.${pkgs.stdenv.hostPlatform.system}.sway;
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
            wayland-scanner
            scdoc

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
            wlroots_0_19
            libdrm
            libxcb-wm
          ];
        };
      });
    };
}
