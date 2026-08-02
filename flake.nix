{
  description = "Victron BLE telemetry bridge for ESPHome and Meshtastic";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "aarch64-darwin"
        "x86_64-darwin"
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs {
            inherit system;
            config.allowUnfreePredicate =
              pkg:
              builtins.elem (nixpkgs.lib.getName pkg) [
                "1password-cli"
                "trunk-io"
              ];
          };
        in
        {
          default = pkgs.mkShell {
            packages = with pkgs; [
              python313
              uv
              git
              esptool
              SDL2
              imagemagick
              nixfmt
              trunk-io
              pkg-config
              _1password-cli
            ];
            UV_PYTHON = "${pkgs.python313}/bin/python3";
            # Nix's esptool package propagates its Python 3.14 dependencies via
            # PYTHONPATH. Keep those packages out of uv's Python 3.13 virtualenv;
            # the wrapped esptool executable still carries its own dependencies.
            shellHook = ''
              unset PYTHONPATH
              if [[ -f pyproject.toml && -f uv.lock ]]; then
                uv sync --locked
              fi
            '';
          };
        }
      );

      checks = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          formatting =
            pkgs.runCommand "formatting"
              {
                nativeBuildInputs = [ pkgs.nixfmt ];
              }
              ''
                nixfmt --check ${./flake.nix}
                touch $out
              '';
        }
      );
    };
}
