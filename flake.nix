{
  description = "Koshka - the fastest cross-platform Bash and POSIX-compatible shell";

  inputs = {
    self.submodules = true;
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      versionLines = nixpkgs.lib.splitString "\n" (builtins.readFile ./src/base/Common.hpp);
      versionValue = name:
        let
          matches = builtins.filter
            (line: builtins.match "#define[[:space:]]+${name}[[:space:]]+.*" line != null)
            versionLines;
          line = if matches == [] then
            throw "Missing ${name} in src/base/Common.hpp"
          else
            builtins.head matches;
        in
        builtins.elemAt
          (builtins.match "#define[[:space:]]+${name}[[:space:]]+(.+)" line)
          0;
      versionSuffix = nixpkgs.lib.removeSuffix "\""
        (nixpkgs.lib.removePrefix "\"" (versionValue "KOSH_VER_SUFFIX"));
      packageVersion = nixpkgs.lib.concatStringsSep "." [
        (versionValue "KOSH_VER_MAJOR")
        (versionValue "KOSH_VER_MINOR")
        (versionValue "KOSH_VER_PATCH")
      ] + versionSuffix;

      linkableMoods = [ "bash" "dash" "sh" ];

      # Each mood becomes a relative link to kosh beside it in bin, as in
      # kosh.override { linkMoods = [ "sh" ]; }.
      mkPackage = nixpkgs.lib.makeOverridable ({ pkgs, mode ? "rel", linkMoods ? [] }:
        assert nixpkgs.lib.assertMsg
          (builtins.all (mood: builtins.elem mood linkableMoods) linkMoods)
          "linkMoods takes bash, dash, or sh";
        pkgs.stdenv.mkDerivation {
          pname = "kosh";
          version = packageVersion;

          src = self;

          nativeBuildInputs = with pkgs; [
            clang
            gnumake
            git
          ];

          buildInputs = nixpkgs.lib.optionals pkgs.stdenv.isLinux [
            pkgs.glibc.static
          ];

          buildPhase = ''
            runHook preBuild
            make -C src -j$NIX_BUILD_CORES MODE=${mode} CXX=${pkgs.clang}/bin/clang++
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp ./kosh $out/bin/
            for mood in ${nixpkgs.lib.escapeShellArgs linkMoods}; do
              ln -s kosh "$out/bin/$mood"
            done
            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "The fastest cross-platform Bash and POSIX-compatible shell";
            homepage = "https://github.com/fennec-support/kosh";
            license = licenses.bsd3;
            mainProgram = "kosh";
            platforms = platforms.unix;
          };
        });

      mkMoodLinks = pkgs: package: linkMoods:
        pkgs.runCommand "kosh-mood-links" { } ''
          mkdir -p $out/bin
          for mood in ${nixpkgs.lib.escapeShellArgs linkMoods}; do
            ln -s ${package}/bin/kosh "$out/bin/$mood"
          done
        '';

      # A string that starts or ends with a blank or starts with a quote is
      # quoted, as kosh(5) requires.
      formatSetting = lib: name: value:
        let
          text =
            if builtins.isBool value then (if value then "on" else "off")
            else if builtins.isInt value then toString value
            else value;
          needsQuotes = text != lib.strings.trim text
            || lib.hasPrefix "\"" text || lib.hasPrefix "'" text;
          quote = if lib.hasInfix "\"" text then "'" else "\"";
        in
        assert lib.assertMsg (!lib.hasInfix "\n" text)
          "programs.kosh.settings.${name} holds a newline";
        "${name}=${if needsQuotes then quote + text + quote else text}";

      mkSystemModule = { config, lib, pkgs, ... }:
        let
          cfg = config.programs.kosh;
          moodLinks = mkMoodLinks pkgs cfg.package cfg.linkMoods;
        in
        {
          options.programs.kosh = {
            enable = lib.mkEnableOption "Koshka - the fastest cross-platform Bash and POSIX-compatible shell";
            package = lib.mkOption {
              type = lib.types.package;
              default = mkPackage { inherit pkgs; };
              description = "The kosh package to use";
            };
            linkMoods = lib.mkOption {
              type = lib.types.listOf (lib.types.enum linkableMoods);
              default = [ ];
              example = [ "sh" "dash" ];
              description = ''
                Moods linked to kosh in the system bin directory. The links
                take priority over another package that provides the same
                command.
              '';
            };
            settings = lib.mkOption {
              type = with lib.types; attrsOf (oneOf [ bool int str ]);
              default = { };
              example = { mood = "bash"; "completion.on_tab" = false; };
              description = ''
                Settings written to /etc/kosh/kosh.conf. A user file and
                KOSHCONF replace each setting they also set.
              '';
            };
          };

          config = lib.mkIf cfg.enable {
            environment.systemPackages = [ cfg.package ]
              ++ lib.optional (cfg.linkMoods != [ ]) (lib.hiPrio moodLinks);
            environment.shells = [ "${cfg.package}/bin/kosh" ]
              ++ map (mood: "${moodLinks}/bin/${mood}") cfg.linkMoods;
            environment.etc."kosh/kosh.conf" = lib.mkIf (cfg.settings != { }) {
              text = lib.concatStringsSep "\n"
                (lib.mapAttrsToList (formatSetting lib) cfg.settings) + "\n";
            };
          };
        };
    in
    {
      packages = forAllSystems (system: let pkgs = nixpkgs.legacyPackages.${system}; in {
        default = self.packages.${system}.kosh;
        kosh = mkPackage { inherit pkgs; };
      });

      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.kosh ];

            packages = with pkgs; [
              clang
              clang-tools
              gnumake
              git
            ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux (with pkgs; [
              mold
              lld
            ]);

            shellHook = ''
              echo "  Build:  make -C src MODE=dbg"
              echo "  Test:   make -C test test"
            '';
          };
        }
      );

      overlays.default = final: prev: {
        kosh = mkPackage { pkgs = final; };
      };

      nixosModules.default = mkSystemModule;

      darwinModules.default = mkSystemModule;

      homeModules.default = { config, lib, pkgs, ... }:
        let
          cfg = config.programs.kosh;
        in
        {
          options.programs.kosh = {
            enable = lib.mkEnableOption "Koshka - the fastest cross-platform Bash and POSIX-compatible shell";
            package = lib.mkOption {
              type = lib.types.package;
              default = mkPackage { inherit pkgs; };
              description = "The kosh package to use";
            };
            linkMoods = lib.mkOption {
              type = lib.types.listOf (lib.types.enum linkableMoods);
              default = [ ];
              example = [ "sh" "dash" ];
              description = "Moods linked to kosh beside it in the profile bin directory";
            };
          };

          config = lib.mkIf cfg.enable {
            home.packages = [ cfg.package ]
              ++ lib.optional (cfg.linkMoods != [ ])
                (lib.hiPrio (mkMoodLinks pkgs cfg.package cfg.linkMoods));
          };
        };
    };
}
