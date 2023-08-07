{ pkgs ? import <nixpkgs> { } }:
with pkgs; mkShell rec {
  #nativeBuildInputs = [
  #  pkg-config
  #];
  buildInputs = [
    readline
  ];
  #LD_LIBRARY_PATH = lib.makeLibraryPath buildInputs;
}
