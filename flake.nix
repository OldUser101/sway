{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { self, nixpkgs }:
    let
      shortRev = self.shortRev or self.dirtyShortRev or "unknown";
      overlays = import ./overlay.nix { inherit shortRev; };

      forAllSystems =
        f:
        nixpkgs.lib.genAttrs nixpkgs.lib.systems.flakeExposed (
          system:
          f (
            import nixpkgs {
              inherit system;
              overlays = [ overlays.default ];
            }
          )
        );
    in
    {
      inherit overlays;

      packages = forAllSystems (pkgs: {
        sway-unwrapped = pkgs.sway-unwrapped;
        default = pkgs.sway-unwrapped;
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
