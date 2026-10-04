class Kosh < Formula
  desc "Fast, Bash-compatible shell, interpreter and diagnostic tool"
  homepage "https://github.com/toiletbril/kosh"
  release = File.read(File.expand_path("../src/base/Common.hpp", __dir__))
                .scan(/^#define KOSH_VER_(?:MAJOR|MINOR|PATCH)\s+(\d+)/)
                .join(".")
  version release
  license all_of: ["BSD-3-Clause", "MIT"]

  livecheck do
    url :stable
    strategy :github_latest
  end

  on_macos do
    depends_on arch: :arm64

    url "https://github.com/toiletbril/kosh/releases/download/#{release}/kosh-darwin-aarch64-#{release}"
    sha256 "54713c220d40b2a98c8c3368fd681bc90cda6f2ad8d1c943a757260786d7c0bc"
  end

  on_linux do
    on_arm do
      url "https://github.com/toiletbril/kosh/releases/download/#{release}/kosh-linux-aarch64-#{release}"
      sha256 "08491f6f59785e2794fdce585738d9c026cb1db5901729fb6c331fb7bf9c0957"
    end

    on_intel do
      url "https://github.com/toiletbril/kosh/releases/download/#{release}/kosh-linux-amd64-#{release}"
      sha256 "5f25b35075526b3bbc37c1c6e57d53a52724109a168825092c08e7121d86ee2c"
    end
  end

  resource "kosh.1" do
    url "https://github.com/toiletbril/kosh/releases/download/#{release}/kosh.1.zst"
    sha256 "98c3713b882bff6887723b34c22af4411be45bb4af06bf8bff7f0cd0059125ba"
  end

  resource "kosh.5" do
    url "https://github.com/toiletbril/kosh/releases/download/#{release}/kosh.5.zst"
    sha256 "b5db20d6b4c118484bcd9843550bc0ec1060c7f26c253bed99bc135984a637d3"
  end

  resource "kosh.bash" do
    url "https://github.com/toiletbril/kosh/releases/download/#{release}/kosh.bash"
    sha256 "2108fb15b5c829cc14af691c63f2ecbd92e7d77d6b1f657b6b89b9fb367d1c81"
  end

  def install
    bin.install Dir["kosh-*-#{version}"].first => "kosh"
    resource("kosh.1").stage { man1.install "kosh.1" }
    resource("kosh.5").stage { man5.install "kosh.5" }
    resource("kosh.bash").stage { bash_completion.install "kosh.bash" => "kosh" }
  end

  def caveats
    <<~EOS
      To use kosh as a login shell, add it to /etc/shells:
        echo #{HOMEBREW_PREFIX}/bin/kosh | sudo tee -a /etc/shells
    EOS
  end

  test do
    assert_equal "hello\n", shell_output("#{bin}/kosh -c 'echo hello'")
  end
end
