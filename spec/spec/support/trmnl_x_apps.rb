# frozen_string_literal: true

require "digest"
require "fileutils"
require "open3"
require "tmpdir"

# What the TRMNL X apps spec (spec/core/trmnl_x/apps_spec.rb) shares: the menu, the upload block,
# and the apps it installs.
module TrmnlXApps
  MAIN_MENU = ["Next Screen", "Previous Screen", "Touchbar Tutorial", "Apps...", "Special...", "Settings...",
               "Close"].freeze
  CRASHED = /Guru Meditation|rst:0x(3|c) / # a panic, a software restart
  BLOCK = 64 # APP_UPLOAD_BLOCK: the device answers each block

  # The apps, built with the firmware's apps/build.sh: hello from its apps/, the misbehaving ones
  # from data/apps/.
  class Elfs
    APPS = File.join(Builds::HERE, "data/apps")

    def self.build_script = File.join(Builds::FIRMWARE, "apps/build.sh")

    def initialize
      @dir = Dir.mktmpdir("trmnl-x-apps-")
      dirs = [File.join(Builds::FIRMWARE, "apps/hello"), *Dir[File.join(APPS, "*")]]
      out, status = Open3.capture2e({ "OUT" => @dir }, self.class.build_script, *dirs)
      raise "apps/build.sh failed:\n#{out}" unless status.success?

      @elfs = Dir[File.join(@dir, "*.elf")].to_h { [File.basename(_1, ".elf"), File.binread(_1)] }
    end

    def [](name) = @elfs.fetch(name)

    def digest(names) = Digest::SHA256.hexdigest(names.map { self[_1] }.join)

    def close = FileUtils.rm_rf(@dir)
  end
end
