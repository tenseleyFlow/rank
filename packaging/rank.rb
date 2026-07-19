# Homebrew formula for the tenseleyFlow tap.
class Rank < Formula
  desc "Drop-in GNU sort replacement built on MSD radix sorting"
  homepage "https://github.com/tenseleyFlow/rank"
  url "https://github.com/tenseleyFlow/rank/releases/download/v0.1.1/rank-0.1.1.tar.gz"
  sha256 "RELEASE_SHA256_PENDING"
  license "MIT"

  def install
    system "./configure"
    system "make"
    bin.install "rank"
    man1.install "doc/rank.1"
  end

  test do
    assert_equal "a\nb\n", pipe_output("#{bin}/rank", "b\na\n")
  end
end
