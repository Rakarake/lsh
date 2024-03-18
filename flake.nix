{
  description = "Sample project";
  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "nixpkgs/nixos-unstable";
  };
  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        deps = with pkgs; [
          gcc
          readline
        ];
      in
      {
        #defaultPackage = pkgs.stdenv.mkDerivation {
        #  name = "lsh";
        #  src = ./.;
        #  buildInputs = deps; 
        #  buildPhase = ''
        #    gcc -o lsh main.c $(pkg-config --cflags --libs sdl2)
        #  '';
        #  installPhase = ''
        #    mkdir -p $out/bin
        #    cp sdl2-test $out/bin
        #  '';
        #};
        devShell = pkgs.mkShell {
          buildInputs = deps;
        };
      }
    );
}

