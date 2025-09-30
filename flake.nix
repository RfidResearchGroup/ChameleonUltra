{
  description = "Nix development environment";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    nixpkgs,
    flake-utils,
    ...
  }:
    flake-utils.lib.eachDefaultSystem (system: let
      pkgs = nixpkgs.legacyPackages.${system};
      pythonPackages = pkgs.python3Packages;
    in {
      devShells = {
        default = pkgs.mkShell {
          name = "impurePythonEnv";
          venvDir = "./.venv";

          # Packages required to build the venv
          buildInputs = [
            # The interpreter to use
            pythonPackages.python
            # automatically create the venv
            pythonPackages.venvShellHook
            # get nix to install wxpython as whl doesn't work on NixOS
          ];

          # Runtime packages
          packages = [
            # Python dev tools
            pythonPackages.ipython
            pythonPackages.ipdb
            pkgs.ruff
            pkgs.pyright
          ];
          env = {
            # Make breakpoint() use ipdb instead of the builtin pdb
            PYTHONBREAKPOINT = "ipdb.set_trace";

          };
          postVenvCreation = ''
            unset SOURCE_DATE_EPOCH
            # Install chameleon as an editable package
            pip install -e software
          '';

          # Now we can execute any commands within the virtual environment.
          # This is optional and can be left out to run pip manually.
          postShellHook = ''
            # allow pip to install wheels
            unset SOURCE_DATE_EPOCH
          '';
        };
      };

      formatter = nixpkgs.legacyPackages.${system}.alejandra;
    });
}
