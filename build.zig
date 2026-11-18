const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const mode = b.option([]const u8, "mode", "Make build mode") orelse
        switch (optimize) {
            .debug => "dbg",
            .small => "tinyrel",
            else => "rel",
        };
    const target_name = switch (target.result.os.tag) {
        .linux => "Linux",
        .macos => "Darwin",
        .windows => "Windows_NT",
        else => {
            b.getInstallStep().dependOn(&b.addFail("Make supports Linux, macOS, and Windows targets").step);
            return;
        },
    };
    if (target.result.os.tag == .macos and
        @import("builtin").os.tag == .macos and
        target.result.cpu.arch != @import("builtin").cpu.arch)
    {
        b.getInstallStep().dependOn(&b.addFail("Make does not support native macOS cross-architecture builds").step);
        return;
    }
    if (target.result.os.tag == .windows and
        target.result.cpu.arch != .x86_64)
    {
        b.getInstallStep().dependOn(&b.addFail("Make supports only x86_64 Windows targets").step);
        return;
    }
    const is_windows = target.result.os.tag == .windows;
    const exe_name = b.fmt("kosh{s}{s}", .{
        if (std.mem.eql(u8, mode, "rel")) "" else b.fmt("-{s}", .{mode}),
        if (is_windows) ".exe" else if (std.mem.eql(u8, mode, "cosmo")) ".com" else "",
    });
    const make = b.addSystemCommand(&.{ "make", "-C" });
    make.addDirectoryArg(b.path("src"));
    make.addArg("kosh");
    make.has_side_effects = true;
    make.addArgs(&.{ b.fmt("MODE={s}", .{mode}), b.fmt("TARGET={s}", .{target_name}) });
    if (target.query.cpu_arch) |arch| {
        make.addArg(b.fmt("ARCH={s}", .{@tagName(arch)}));
    }
    if (b.option(bool, "no-toiletline", "Build without the interactive editor") orelse false)
        make.addArg("NO_TOILETLINE=1");
    if (b.option(bool, "no-koshkit", "Build without bundled utilities") orelse false)
        make.addArg("NO_KOSHKIT=1");
    if (b.option(bool, "hardened", "Trap on container bounds violations") orelse (optimize == .safe))
        make.addArg("HARDENED=1");
    if (b.option([]const u8, "pgo", "Make PGO instrumentation mode")) |pgo|
        make.addArg(b.fmt("PGO={s}", .{pgo}));
    if (b.option([]const u8, "pgo-profile", "LLVM profile path")) |profile| {
        const profile_path = if (std.fs.path.isAbsolute(profile))
            b.graph.cwdRelativePath(profile)
        else
            b.path(profile);
        make.addFileArg2(profile_path, .{ .prefix = "PGO_PROFILE=", .make_absolute = true });
    }
    const executable = make.addOutputFileArg2(exe_name, .{ .prefix = "OUTPUT_PATH=", .make_absolute = true });
    const install_exe = b.addInstallBinFile(executable, exe_name);
    b.getInstallStep().dependOn(&install_exe.step);

    if (b.option([]const u8, "link-moods", "Link bash, dash, or sh beside kosh, separated by commas")) |list| {
        addMoodLinks(b, install_exe, executable, exe_name, list, is_windows);
    }

    const run_step = b.step("run", "Build and run kosh");
    const run_cmd = b.addRunFile(executable);
    run_cmd.addPassthruArgs();
    run_step.dependOn(&run_cmd.step);
}

/// each mood becomes a relative link beside the executable on non-Windows
/// targets, where scripts/link-moods.sh rejects unrelated existing files.
/// Windows installs a copy named MOOD.exe.
fn addMoodLinks(b: *std.Build, install_exe: *std.Build.Step.InstallFile, executable: std.Build.LazyPath, exe_name: []const u8, list: []const u8, is_windows: bool) void {
    var moods: std.ArrayList([]const u8) = .empty;
    var names = std.mem.splitScalar(u8, list, ',');

    while (names.next()) |name| {
        if (std.mem.eql(u8, name, "kosh")) continue;
        if (!std.mem.eql(u8, name, "bash") and !std.mem.eql(u8, name, "dash") and !std.mem.eql(u8, name, "sh")) {
            const fail = b.addFail(b.fmt("-Dlink-moods takes bash, dash, sh, or kosh, not '{s}'", .{name}));
            b.getInstallStep().dependOn(&fail.step);
            return;
        }
        moods.append(b.allocator, name) catch @panic("out of memory");
    }

    if (is_windows) {
        for (moods.items) |mood| {
            b.getInstallStep().dependOn(&b.addInstallBinFile(executable, b.fmt("{s}.exe", .{mood})).step);
        }
        return;
    }

    const links = b.addSystemCommand(&.{"sh"});
    links.addFileArg(b.path("scripts/link-moods.sh"));
    links.addDirectoryArg(b.graph.path(.install_bin, ""));
    links.addArg(exe_name);
    links.addArgs(moods.items);
    links.step.dependOn(&install_exe.step);
    b.getInstallStep().dependOn(&links.step);
}
