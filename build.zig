const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});

    const optimize = b.standardOptimizeOption(.{});
    const is_release = optimize != .Debug;
    const mode = if (is_release) "rel" else "dbg";

    const module = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });

    const exe = b.addExecutable(.{
        .name = if (is_release) "kosh" else "kosh-dbg",
        .root_module = module,
    });

    var flags: std.ArrayList([]const u8) = .empty;
    defer flags.deinit(b.allocator);

    const common_flags = [_][]const u8{
        "-std=c++23",
        "-Wall",
        "-Wextra",
        "-Wdouble-promotion",
        "-Wformat",
        "-Wformat-security",
        "-Wno-unused-command-line-argument",
        "-Wno-deprecated",
        "-Wno-unknown-warning-option",
        "-Wno-error=maybe-uninitialized",
        "-fvisibility=hidden",
        "-fno-rtti",
        "-fno-semantic-interposition",
        "-Wno-unknown-pragmas",
        "-fno-strict-aliasing",
        "-Wno-date-time",
    };
    flags.appendSlice(b.allocator, &common_flags) catch @panic("out of memory");

    const commit_hash = gitCommitHash(b);
    const os_info = b.fmt("zig {f} {s}", .{ @import("builtin").zig_version, systemInfo(b) });

    const defines = [_][]const u8{
        "-DKOSH_COMPILER_COMMAND=\"zig c++\"",
        "-DKOSH_LIBC=\"zig libc++\"",
        b.fmt("-DKOSH_OS_INFO=\"{s}\"", .{os_info}),
        b.fmt("-DKOSH_COMMIT_HASH=\"{s}\"", .{commit_hash}),
        b.fmt("-DKOSH_BUILD_MODE=\"{s}\"", .{mode}),
        "-DKOSH_ENVCXXFLAGS=\"\"",
    };
    flags.appendSlice(b.allocator, &defines) catch @panic("out of memory");
    if (!is_release) {
        flags.append(b.allocator, "-DKOSH_DEBUG_SYSTEM_KOSHCONF_OVERRIDE") catch @panic("out of memory");
    }

    // As in src/Makefile, release builds protect the stack everywhere, add
    // stack clash probes and control-flow protection on Linux, and link a
    // position-independent, eagerly bound image there.
    if (is_release) {
        flags.append(b.allocator, "-fstack-protector-strong") catch @panic("out of memory");
        if (target.result.os.tag == .linux) {
            flags.append(b.allocator, "-fstack-clash-protection") catch @panic("out of memory");
            switch (target.result.cpu.arch) {
                .x86_64 => flags.append(b.allocator, "-fcf-protection=full") catch @panic("out of memory"),
                .aarch64 => flags.append(b.allocator, "-mbranch-protection=standard") catch @panic("out of memory"),
                else => {},
            }
            exe.pie = true;
            exe.link_z_relro = true;
            exe.link_z_lazy = false;
        }
    }

    const sources = collectSources(b);
    module.addCSourceFiles(.{
        .root = b.path("src"),
        .files = sources,
        .flags = flags.items,
    });

    // As in src/Makefile, Windows links the vendored regex and the system
    // libraries that the GNU ABI leaves out, and Darwin links two frameworks.
    switch (target.result.os.tag) {
        .windows => {
            module.addCSourceFiles(.{
                .root = b.path("vendor/regex"),
                .files = &.{ "regcomp.c", "regerror.c", "regexec.c", "tre-mem.c" },
                .flags = &.{"-std=c11"},
            });
            module.addIncludePath(b.path("vendor/regex"));
            for ([_][]const u8{ "userenv", "advapi32", "shell32", "ole32", "uuid", "iphlpapi", "rstrtmgr", "ws2_32" }) |library| {
                module.linkSystemLibrary(library, .{});
            }
            exe.mingw_unicode_entry_point = true;
        },
        .macos => {
            module.linkFramework("CoreFoundation", .{});
            module.linkFramework("IOKit", .{});
        },
        else => {},
    }

    const install_exe = b.addInstallArtifact(exe, .{});
    b.getInstallStep().dependOn(&install_exe.step);

    if (b.option([]const u8, "link-moods", "Link bash, dash, or sh to kosh in the bin directory, separated by commas")) |list| {
        addMoodLinks(b, install_exe, list, target.result.os.tag == .windows);
    }

    const run_step = b.step("run", "Build and run kosh");
    const run_cmd = b.addRunArtifact(exe);
    if (b.args) |args| run_cmd.addArgs(args);
    run_step.dependOn(&run_cmd.step);
}

/// Each mood becomes a relative link to the executable beside it in bin, or a
/// copy named MOOD.exe on Windows. A file that is not such a link stays and
/// fails the install.
fn addMoodLinks(b: *std.Build, install_exe: *std.Build.Step.InstallArtifact, list: []const u8, is_windows: bool) void {
    const exe = install_exe.artifact;
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
            b.getInstallStep().dependOn(&b.addInstallBinFile(exe.getEmittedBin(), b.fmt("{s}.exe", .{mood})).step);
        }
        return;
    }

    const links = b.allocator.create(MoodLinks) catch @panic("out of memory");
    links.* = .{
        .step = std.Build.Step.init(.{ .id = .custom, .name = "link moods", .owner = b, .makeFn = MoodLinks.make }),
        .exe_name = exe.name,
        .moods = moods.items,
    };
    links.step.dependOn(&install_exe.step);
    b.getInstallStep().dependOn(&links.step);
}

const MoodLinks = struct {
    step: std.Build.Step,
    exe_name: []const u8,
    moods: []const []const u8,

    fn make(step: *std.Build.Step, options: std.Build.Step.MakeOptions) anyerror!void {
        _ = options;
        const links: *MoodLinks = @fieldParentPtr("step", step);
        const b = step.owner;
        const io = b.graph.io;
        const bin_path = b.getInstallPath(.bin, "");
        var dir = try std.Io.Dir.cwd().openDir(io, bin_path, .{});
        defer dir.close(io);

        for (links.moods) |mood| {
            dir.symLink(io, links.exe_name, mood, .{}) catch |err| switch (err) {
                error.PathAlreadyExists => {
                    var buffer: [std.fs.max_path_bytes]u8 = undefined;
                    const length = dir.readLink(io, mood, &buffer) catch 0;
                    if (!std.mem.eql(u8, buffer[0..length], links.exe_name)) {
                        return step.fail("{s}/{s} is not a link to {s}", .{ bin_path, mood, links.exe_name });
                    }
                },
                else => |e| return e,
            };
        }
    }
};

fn gitCommitHash(b: *std.Build) []const u8 {
    const result = std.process.run(b.allocator, b.graph.io, .{
        .argv = &.{ "git", "rev-parse", "HEAD" },
        .cwd = .{ .path = b.build_root.path orelse "." },
    }) catch return "<no git>";
    if (result.term != .exited or result.term.exited != 0) return "<no git>";
    return std.mem.trim(u8, result.stdout, " \n\r\t");
}

fn systemInfo(b: *std.Build) []const u8 {
    const result = std.process.run(b.allocator, b.graph.io, .{
        .argv = &.{ "uname", "-srvom" },
        .cwd = .{ .path = b.build_root.path orelse "." },
    }) catch return @tagName(@import("builtin").target.os.tag);
    if (result.term != .exited or result.term.exited != 0)
        return @tagName(@import("builtin").target.os.tag);
    return std.mem.trim(u8, result.stdout, " \n\r\t");
}

fn collectSources(b: *std.Build) []const []const u8 {
    var sources: std.ArrayList([]const u8) = .empty;

    var dir = b.build_root.handle.openDir(
        b.graph.io,
        "src",
        .{ .iterate = true },
    ) catch @panic("unable to open the src directory");
    defer dir.close(b.graph.io);

    var walker = dir.walk(b.allocator) catch @panic("out of memory");
    defer walker.deinit();

    while (walker.next(b.graph.io) catch @panic("unable to walk src")) |entry| {
        if (entry.kind != .file) continue;
        if (!std.mem.endsWith(u8, entry.path, ".cpp")) continue;
        // As in src/Makefile, sources sit in src or one directory below it,
        // and the routed platform fragments compile through Platform.cpp.
        if (std.mem.count(u8, entry.path, std.fs.path.sep_str) > 1) continue;
        if (std.mem.startsWith(u8, std.fs.path.basename(entry.path), "PlatformPosix")) continue;
        if (std.mem.startsWith(u8, std.fs.path.basename(entry.path), "PlatformWin32")) continue;
        sources.append(b.allocator, b.dupe(entry.path)) catch @panic("out of memory");
    }

    return sources.toOwnedSlice(b.allocator) catch @panic("out of memory");
}
