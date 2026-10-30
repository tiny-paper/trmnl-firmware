# frozen_string_literal: true

require "zlib"

# TRMNL X apps (docs/apps.md in the firmware): ELF apps in /apps, listed in the touch menu's
# Apps... submenu, copied in over the USB serial port ("Receive over USB...", the protocol of
# apps/upload.py), ended by a long press, resumed after a restart only when input reached them,
# and never started again after crashing as they start. Needs a firmware with the app loader
# (apps/build.sh builds the apps) and the simulator's POST /serial; skipped without them.

RSpec.describe "TRMNL X apps", env: "TRMNL_X" do
  fixture(:shipped) { TrmnlX::ShippedX.new }
  fixture(:dev) { TrmnlX::ProvisionedX.new(shipped) }
  fixture(:elfs) { TrmnlXApps::Elfs.new }

  before do
    build_script = TrmnlXApps::Elfs.build_script
    skip "no app loader in this firmware (#{build_script})" unless File.exist?(build_script)
    dev.reset
  end

  def apps_menu(apps) = apps.sort + ["Receive over USB...", "Install via Wi-Fi", "Back"]

  def since(s) = s.status["console_total"]

  # On a menu screen showing `items` with the arrow on the first, move right to `item` and choose
  # it; a submenu ("...") is waited for until drawn.
  def choose(s, items, item)
    items.take(items.index(item) + 1).each_cons(2) do |from, to|
      TrmnlX.menu_tap(s, "right", /Menu: #{Regexp.escape(from)} -> #{Regexp.escape(to)}/)
    end
    TrmnlX.menu_tap(s, "center", /Menu: chose #{Regexp.escape(item)}/,
                    screen: item.end_with?("...") && item != "Receive over USB...")
  end

  # From deep sleep: a middle tap, Apps..., then `item` of it (`apps`: the installed apps).
  def open_apps_item(s, apps, item)
    c = since(s)
    s.touch("center", ms: 150)
    s.wait(console: /menu_show_screen end/, since: c, timeout: 30)
    choose(s, TrmnlXApps::MAIN_MENU, "Apps...")
    choose(s, apps_menu(apps), item)
  end

  # The device's next "TAPP ..." answer on the serial port.
  def reply(s, timeout: 10)
    text = s.wait(console: /TAPP (READY|ACK|OK|ERR)/, timeout:)["line"]["text"]
    text[text.index("TAPP ")..].strip
  end

  # apps/upload.py's upload: a header, then the data in blocks, each answered.
  def upload(s, name, data)
    s.serial(format("TAPP PUT %<name>s %<size>d %<crc>08x\n", name:, size: data.bytesize, crc: Zlib.crc32(data)))
    expect(reply(s)).to eq("TAPP READY")
    data.bytes.each_slice(TrmnlXApps::BLOCK).with_index(1) do |block, i|
      s.serial(block.pack("C*"))
      sent = [i * TrmnlXApps::BLOCK, data.bytesize].min
      expect(reply(s)).to eq(sent < data.bytesize ? "TAPP ACK #{sent}" : "TAPP OK #{name}")
    end
  end

  def receive_over_usb(s, apps)
    open_apps_item(s, apps, "Receive over USB...")
    s.wait(console: /Apps: waiting for an upload on the USB serial port/, timeout: 30)
  end

  # Leave "Receive over USB..." with a middle tap: back on the Apps submenu, arrow on its first item.
  def stop_receiving(s)
    c = since(s)
    s.wait(display_idle: true, settle_ms: 300, timeout: 30)
    s.touch("center", ms: 150)
    s.wait(console: /Apps: upload mode ended/, since: c, timeout: 15)
    s.wait(console: /menu_show_screen end/, since: c, timeout: 30)
  end

  # A 2 s hold in the middle: the app is asked to exit. The bar must have seen the last tap end
  # (a touch right after it reads as that one, held) and the panel be idle.
  def long_press(s)
    s.wait(display_idle: true, settle_ms: 300, timeout: 30)
    s.touch("center", ms: 2000)
  end

  def count(s, regex) = s.console(0).count { regex.match?(_1) }

  # The provisioned X asleep with `names` installed over USB (kept in the setup cache).
  def with_apps(names, &)
    inputs = { elfs: elfs.digest(names), support: SetupCache.file_hash(__FILE__) }
    state = dev.cached_state("apps-#{names.join('-')}", inputs) do |s|
      s.wait(state: "deep_sleep", timeout: 120)
      receive_over_usb(s, [])
      names.each { upload(s, "#{_1}.elf", elfs[_1]) }
      stop_receiving(s)
      s.wait(state: "deep_sleep", timeout: 120, settle_ms: 500) # the menu times out
    end
    dev.restore(state) do |s|
      s.wait(state: "deep_sleep", timeout: 15)
      yield s
    end
  end

  describe "Install" do
    it "takes an app over USB, lists it and runs it" do
      dev.boot do |s|
        s.wait(state: "deep_sleep", timeout: 120)
        receive_over_usb(s, [])
        upload(s, "hello.elf", elfs["hello"])
        s.wait(console: %r{Apps: received /apps/hello\.elf, #{elfs['hello'].bytesize} bytes}, since: 0, timeout: 5)
        stop_receiving(s)
        choose(s, apps_menu(["hello"]), "hello")
        s.wait(console: /app hello: hello: run 1/, timeout: 60)
        long_press(s)
        s.wait(console: /Apps: hello ended with 0/, timeout: 30)
        s.wait(state: "deep_sleep", timeout: 120)
        expect(count(s, TrmnlXApps::CRASHED)).to eq(0)
      end
    end

    it "keeps the old app after a bad, cut-off or refused upload" do
      with_apps(["hello"]) do |s|
        receive_over_usb(s, ["hello"])
        other = Random.new(1).bytes(3000)
        s.serial(format("TAPP PUT hello.elf %<size>d 12345678\n", size: other.bytesize)) # wrong checksum
        expect(reply(s)).to eq("TAPP READY")
        answers = other.bytes.each_slice(TrmnlXApps::BLOCK).map do |block|
          s.serial(block.pack("C*"))
          reply(s)
        end
        expect(answers.last).to eq("TAPP ERR bad crc")
        s.serial("TAPP PUT hello.elf #{other.bytesize} 0\n") # cut off: one block, then nothing
        expect(reply(s)).to eq("TAPP READY")
        s.serial(other.byteslice(0, TrmnlXApps::BLOCK))
        expect(reply(s)).to eq("TAPP ACK #{TrmnlXApps::BLOCK}")
        expect(reply(s, timeout: 30)).to eq("TAPP ERR short")
        { "TAPP PUT ../x.elf 10 0" => "bad name", "TAPP PUT hello.elf 0 0" => "empty",
          "TAPP PUT hello.elf 2097153 0" => "too big", "garbage" => "bad header" }.each do |header, why|
          s.serial("#{header}\n")
          expect(reply(s)).to eq("TAPP ERR #{why}")
        end
        stop_receiving(s)
        choose(s, apps_menu(["hello"]), "hello")
        s.wait(console: /app hello: hello: run 1/, timeout: 60)
        long_press(s)
        s.wait(console: /Apps: hello ended with 0/, timeout: 30)
        s.wait(state: "deep_sleep", timeout: 120)
        expect(count(s, TrmnlXApps::CRASHED)).to eq(0)
      end
    end

    it "ends Install via Wi-Fi on a long press" do
      with_apps(["hello"]) do |s|
        open_apps_item(s, ["hello"], "Install via Wi-Fi")
        s.wait(console: /Apps: install screen shown/, timeout: 120)
        s.wait(display_idle: true, settle_ms: 300, timeout: 30)
        c = since(s)
        long_press(s)
        s.wait(console: /Touch: ch1 long press/, since: c, timeout: 15)
        s.wait(state: "deep_sleep", timeout: 120)
        expect(count(s, TrmnlXApps::CRASHED)).to eq(0)
      end
    end
  end

  describe "Run" do
    it "starts from the menu, takes input and returns to TRMNL on a long press" do
      with_apps(["hello"]) do |s|
        open_apps_item(s, ["hello"], "hello")
        s.wait(console: %r{Apps: Hello \(/apps/hello\.elf\) running}, timeout: 60)
        s.wait(console: /app hello: hello: run 1/, timeout: 30)
        s.touch("left", ms: 150)
        s.wait(console: /hello: touch left tap/, timeout: 15)
        s.serial("a\e[A")
        s.wait(console: /hello: key down 'a' code 512/, timeout: 15)
        s.wait(console: /hello: key down code 256/, timeout: 15)
        long_press(s)
        s.wait(console: /Apps: long press - asking hello to exit/, timeout: 15)
        s.wait(console: /hello: exit requested/, timeout: 15)
        s.wait(console: /Apps: hello ended with 0/, timeout: 15)
        s.wait(state: "deep_sleep", timeout: 120)
        expect(count(s, TrmnlXApps::CRASHED)).to eq(0)
        expect(s.preferences["app_resume"]).to eq("0")
      end
    end

    it "restarts into TRMNL when an app ignores the long press" do
      with_apps(["stuck"]) do |s|
        open_apps_item(s, ["stuck"], "stuck")
        s.wait(console: /stuck: running/, timeout: 60)
        long_press(s)
        s.wait(console: /stuck: ignoring the exit request/, timeout: 15)
        s.wait(console: /Apps: stuck did not exit within 2000 ms - restarting into TRMNL/, timeout: 15)
        s.wait(state: "deep_sleep", timeout: 300)
        expect(count(s, /Apps: resuming/)).to eq(0)
        expect(count(s, /stuck: running/)).to eq(1)
      end
    end
  end

  describe "Resume" do
    it "resumes an app that input reached after a restart" do
      with_apps(["hello"]) do |s|
        open_apps_item(s, ["hello"], "hello")
        s.wait(console: /app hello: hello: run 1/, timeout: 60)
        s.serial("a")
        s.wait(console: /hello: key down 'a'/, timeout: 15)
        sleep_virtual(s, 30) # past APP_RESUME_STARTED_MS (5 s)
        expect(s.preferences["app_resume"]).to eq("1")
        c = since(s)
        s.reset
        s.wait(console: %r{Apps: resuming /apps/hello\.elf}, since: c, timeout: 120)
        s.wait(console: /app hello: hello: run 2/, since: c, timeout: 60)
        long_press(s)
        s.wait(console: /Apps: hello ended with 0/, timeout: 15)
        s.wait(state: "deep_sleep", timeout: 120)
        expect(s.preferences["app_resume"]).to eq("0")
      end
    end

    it "does not resume an app no input reached" do
      with_apps(["hello"]) do |s|
        open_apps_item(s, ["hello"], "hello")
        s.wait(console: /app hello: hello: run 1/, timeout: 60)
        sleep_virtual(s, 8)
        expect(s.preferences["app_resume"]).to eq("1")
        c = since(s)
        s.reset
        s.wait(console: %r{Apps: not resuming /apps/hello\.elf: no touch or key reached it}, since: c, timeout: 120)
        s.wait(state: "deep_sleep", timeout: 120)
        expect(count(s, /Apps: resuming/)).to eq(0)
        expect(s.preferences["app_resume"]).to eq("0")
      end
    end

    it "never starts an app again that crashed as it started" do
      with_apps(["crash"]) do |s|
        s.set_preference("app_last", "string", "/apps/crash.elf") # as if it had been running fine
        s.set_preference("app_resume", "u8", 1)
        s.set_preference("app_input", "u8", 1) # and a touch or key had reached it
        c = since(s)
        s.reset
        s.wait(console: %r{Apps: resuming /apps/crash\.elf}, since: c, timeout: 120)
        s.wait(console: /crash: crashing now/, since: c, timeout: 60)
        s.wait(console: /Guru Meditation/, since: c, timeout: 60)
        s.wait(console: /Apps: reset by a crash/, since: c, timeout: 120)
        s.wait(state: "deep_sleep", timeout: 300)
        expect(count(s, /crash: crashing now/)).to eq(1)
        expect(count(s, /Apps: resuming/)).to eq(1)
      end
    end

    it "gives a clean error for a missing app to resume" do
      with_apps(["hello"]) do |s|
        s.set_preference("app_last", "string", "/apps/gone.elf")
        s.set_preference("app_resume", "u8", 1)
        s.set_preference("app_input", "u8", 1) # and a touch or key had reached it
        c = since(s)
        s.reset
        s.wait(console: /Apps: gone: file not found/, since: c, timeout: 120)
        s.wait(state: "deep_sleep", timeout: 120)
        expect(s.preferences["app_resume"]).to eq("0")
      end
    end
  end

  # Let `seconds` of virtual time pass (a wait for a line that never comes).
  def sleep_virtual(s, seconds)
    s.wait(console: /\A\z never/, since: since(s), timeout: seconds)
  rescue TrmnlSim::Error
    nil
  end
end
