{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.olduser101-wlroots.url = "github:OldUser101/wlroots";
  inputs.olduser101-wlroots.inputs.nixpkgs.follows = "nixpkgs";

  outputs =
    { self, nixpkgs, olduser101-wlroots }:
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
              overlays = [
                olduser101-wlroots.overlays.default
                overlays.default
              ];
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
            wlroots
            libdrm
            libxcb-wm
          ];
        };
      });
    };
}
