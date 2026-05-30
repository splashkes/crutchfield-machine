class CrutchfieldMachine < Formula
  desc "GPU video-feedback machine with OSC / MIDI / Launch Control + math dashboard"
  homepage "https://github.com/splashkes/crutchfield-machine"
  url "https://github.com/splashkes/crutchfield-machine.git",
      branch: "main"
  version "0.1.0"
  license "MIT"

  depends_on "glew"
  depends_on "glfw"
  depends_on "pkg-config" => :build
  depends_on :macos
  depends_on :xcode => :build

  on_macos do
    # The bundled Makefile.macos targets Homebrew-installed glfw + glew on
    # Apple Silicon. Builds in-place; install copies the binary + the
    # runtime asset tree (shaders, presets, music, ui.yaml, js).
    def install
      system "make", "-f", "Makefile.macos", "BREW_PREFIX=#{HOMEBREW_PREFIX}"

      libexec.install "feedback"

      pkgshare.install "shaders"
      pkgshare.install "presets"
      pkgshare.install "music"
      pkgshare.install "js"
      pkgshare.install "ui.yaml"
      pkgshare.install "bindings.examples"

      # Wrapper script chdir's into the asset tree so the executable's
      # relative-path lookups for shaders/ and presets/ resolve.
      (bin/"feedback").write <<~SHELL
        #!/usr/bin/env bash
        cd "#{pkgshare}"
        exec "#{libexec}/feedback" "$@"
      SHELL
      chmod 0755, bin/"feedback"
    end
  end

  test do
    # --list-actions exits 0 and prints over 100 action names
    out = shell_output("#{bin}/feedback --list-actions")
    assert out.lines.count > 100
    assert_match(/layer.warp/, out)
    assert_match(/dyn.decay.axis/, out)
    assert_match(/snapshot.save/, out)
  end
end
