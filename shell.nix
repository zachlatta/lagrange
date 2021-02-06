{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    pkg-config

    cmake
    SDL2
    openssl
    pcre.dev
    zlib
    libunistring
  ];
}
