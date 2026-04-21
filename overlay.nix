{
  shortRev ? "dirty",
  ...
}:
rec {
  default = sway-unwrapped;

  sway-unwrapped = final: prev: {
    sway-unwrapped = prev.callPackage ./package.nix {
      version = "olduser101-git-${shortRev}";
    };
  };
}
