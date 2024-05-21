// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2016-2024 Egor Pugin <egor.pugin@gmail.com>

/*
c++: 23
package_definitions: true
dependencies:
    - pub.egorpugin.primitives.command
    - pub.egorpugin.primitives.executor
    - pub.egorpugin.primitives.filesystem
    - pub.egorpugin.primitives.sw.main
    - pub.egorpugin.primitives.win32helpers
*/

#include <primitives/command.h>
#include <primitives/executor.h>
#include <primitives/filesystem.h>
#include <primitives/win32helpers.h>
#include <primitives/sw/main.h>
#include <boost/algorithm/string.hpp>

#include <iostream>
#include <regex>

// NOTE: we may want to create .com files in some cases?

WORD get_file_subsystem(LPCTSTR filename)
{
    auto hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        //printf("Couldn't open file with CreateFile()\n");
        return 0;
    }

    auto hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hFileMapping == 0)
    {
        CloseHandle(hFile);
        //printf("Couldn't open file mapping with CreateFileMapping()\n");
        return 0;
    }

    auto lpFileBase = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
    if (lpFileBase == 0)
    {
        CloseHandle(hFileMapping);
        CloseHandle(hFile);
        //printf("Couldn't map view of file with MapViewOfFile()\n");
        return 0;
    }

    auto dosHeader = (PIMAGE_DOS_HEADER)lpFileBase;
    auto fh = (PIMAGE_NT_HEADERS)((char*)dosHeader + dosHeader->e_lfanew);
    auto ss = fh->OptionalHeader.Subsystem;

    UnmapViewOfFile(lpFileBase);
    CloseHandle(hFileMapping);
    CloseHandle(hFile);

    return ss;
}

void remove_old_files(auto &&files, const path &dir)
{
    // get new files
    Files dstfiles;
    for (auto &[k, v] : files)
        dstfiles.insert(v.alias);

    // get existing links
    auto existing_files = enumerate_files(dir);
    for (auto &f : existing_files)
    {
        if (dstfiles.find(f.filename()) == dstfiles.end())
        {
            std::cout << "Removing old file: " << f << "\n";
            fs::remove(f);
        }
    }
}

void create_links(auto &&files, const path &dstdir, const auto &compiler, const path &exe_source_fn)
{
    const auto obj = fs::temp_directory_path() / "path" / "obj";
    fs::create_directories(obj);

    Executor e("Proxy creator");
    std::vector<Future<void>> futures;
    for (auto &f : files)
    {
        futures.push_back(e.push([&obj, &dstdir, &compiler, &exe_source_fn, p = f.first, name = f.second.alias, opts = f.second.opts]
        {
            auto o = dstdir / name;
            if (fs::exists(o))
                return;

            if (p.extension() == ".bat" || p.extension() == ".cmd")
            {
                write_file(o, "@echo off\npushd .\ncall \"" + p.string() + "\" %*\npopd\n");
                return;
            }

            auto ss = get_file_subsystem(normalize_path(p).c_str());

            Strings args
            {
                compiler.cl.string(),
                exe_source_fn.string(),
                "/Fo" + normalize_path(obj / name).string(),
                "/Fe" + normalize_path(fs::current_path() / o).string(),
                "/nologo",
                "/std:c++latest",
                "/EHsc",
                // share pdb
                "/FS",

    #ifndef NDEBUG
                "/Od",
                "/Zi",
                "/DDEBUG_EXE",
    #else
                "/O2",
    #endif

                "/DUNICODE",
                "/DCONSOLE="s + (ss == IMAGE_SUBSYSTEM_WINDOWS_GUI ? "0" : "1"),
                "/DPROG=LR\"myfile(" + boost::replace_all_copy(normalize_path(p.parent_path()).string(), "/", "\\") + "\\" + normalize_path(p.filename()).string() + ")myfile\"",
                "/DPROG_NAME=LR\"myfile(" + normalize_path(p.filename()).string() + ")myfile\"",
            };

            if (opts.add_parent_directory_to_path) {
                args.push_back("/DADD_PARENT_DIRECTORY_TO_PATH");
            }

            for (auto &&i : compiler.idirs) {
                args.push_back("-I"s + i.string());
            }

            args.push_back("-link");

            for (auto &&i : compiler.ldirs) {
                args.push_back("-LIBPATH:"s + i.string());
            }

    #ifndef NDEBUG
            args.push_back("-debug:full");
    #endif

            args.push_back("Shell32.lib");
            if (ss == IMAGE_SUBSYSTEM_WINDOWS_GUI)
                args.push_back("/SUBSYSTEM:WINDOWS");

            primitives::Command c;
            // so created pdf in debug mode will be created in that dir
            c.working_directory = dstdir;
    #ifndef NDEBUG
            c.inherit = true;
    #endif
            c.setArguments(args);
            c.execute();
        }));
    }
    waitAndGet(futures);
}

struct cl_desc {
    path cl;
    std::vector<path> idirs;
    std::vector<path> ldirs;
};

cl_desc find_cl_exe() {
    auto insts = EnumerateVSInstances();
    for (auto &&i : insts) {
        if (i.VSInstallLocation.contains(L"Preview")) {
            continue;
        }
        path p = i.VSInstallLocation;
        p = p / "VC" / "Tools" / "MSVC";
        for (auto &&d : fs::directory_iterator{p}) {
            if (!fs::is_directory(d)) {
                continue;
            }
            auto p2 = p / d.path() / "bin";
            cl_desc c;
            auto check_and_ret = [&](auto &&in) {
                if (fs::exists(in)) {
                    c.cl = in;
                    c.idirs.push_back(p / d.path() / "include");
                    path kit;
                    {
                        path kitsdir = "C:\\Program Files (x86)\\Windows Kits\\10\\include";
                        std::set<path> kits;
                        for (auto &&d : fs::directory_iterator{kitsdir}) {
                            if (!fs::is_directory(d) || !isdigit(d.path().filename().string()[0])) {
                                continue;
                            }
                            kits.insert(d.path().filename());
                        }
                        kit = *kits.rbegin();
                        c.idirs.push_back(kitsdir / kit / "ucrt");
                        c.idirs.push_back(kitsdir / kit / "um");
                        c.idirs.push_back(kitsdir / kit / "shared");
                    }
                    c.ldirs.push_back(p / d.path() / "lib" / "x64");
                    {
                        path kitsdir = "C:\\Program Files (x86)\\Windows Kits\\10\\lib";
                        c.ldirs.push_back(kitsdir / kit / "ucrt" / "x64");
                        c.ldirs.push_back(kitsdir / kit / "um" / "x64");
                    }
                    return true;
                }
                return false;
            };
            if (false
                || check_and_ret(p2 / "Hostx64" / "x64" / "cl.exe")
                //|| check_and_ret(p2 / "Hostx86" / "x64" / "cl.exe")
                //|| check_and_ret(p2 / "Hostx64" / "x86" / "cl.exe")
                //|| check_and_ret(p2 / "Hostx86" / "x86" / "cl.exe")
            ) {
                return c;
            }
        }
    }
    return {};
}

struct options {
    bool add_parent_directory_to_path{};
};

struct data {
    // grab all *.exe, *.bat, *.cmd
    std::vector<std::string> all;
    // grab all files by regex
    std::vector<std::pair<std::string, std::string>> regex;
    // grab selected files and optional alias=new name
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> files;
    // same but with more options
    struct file_alias {
        std::string filename;
        std::string alias;
    };
    struct file_aliases {
        std::vector<file_alias> aliases;
        options opts{};
    };
    std::map<std::string, file_aliases> files_with_options;

    bool empty() const {
        return all.empty() && regex.empty() && files.empty() && files_with_options.empty();
    }
} root
// rename to data.h?
#if __has_include("path.h")
= {
#include "path.h"
}
#endif
;

int main(int argc, char *argv[]) {
    const auto cl = find_cl_exe();
    const path exe = fs::absolute("exe.cpp");

    if (cl.cl.empty())
        throw std::runtime_error("Visual Studio compiler (cl.exe) was not found");
    if (!fs::exists(exe))
        throw std::runtime_error("exe.cpp was not found");

    // <existing file path, alias filename>
    struct alias {
        path alias;
        options opts{};
    };
    std::unordered_multimap<path, alias> files;

    //
    for (auto &p : root.all) {
        if (!fs::exists(p)) {
            std::cout << p << " does not exist\n";
            continue;
        }
        for (auto &file : fs::directory_iterator{p}) {
            if (!fs::is_regular_file(file) || (file.path().extension() != ".exe" && file.path().extension() != ".bat" &&
                                               file.path().extension() != ".cmd")) {
                continue;
            }
            files.emplace(file.path(), file.path().filename());
        }
    }
    for (auto &re : root.regex) {
        auto &&p = re.first;
        if (!fs::exists(p)) {
            std::cout << p << " does not exist\n";
            continue;
        }
        std::regex r{re.second};
        for (auto &file : fs::directory_iterator{p}) {
            if (!fs::is_regular_file(file))
                continue;
            if (!std::regex_match(file.path().string(), r))
                continue;
            files.emplace(file.path(), file.path().filename());
        }
    }
    for (auto &&[pp, kv] : root.files) {
        path p = pp;
        for (auto &&[fn, alias] : kv) {
            if (alias.empty())
                files.emplace(p / fn, fn);
            else
                files.emplace(p / fn, alias);
        }
    }
    for (auto &&[pp, kv] : root.files_with_options) {
        path p = pp;
        for (auto &&[fn, alias] : kv.aliases) {
            if (alias.empty()) {
                auto it = files.emplace(p / fn, fn);
                it->second.opts = kv.opts;
            } else {
                auto it = files.emplace(p / fn, alias);
                it->second.opts = kv.opts;
            }
        }
    }

    const path dst = "links";
    fs::create_directories(dst);
    if (!root.empty()) {
        remove_old_files(files, dst);
    }
    create_links(files, dst, cl, exe);

    return 0;
}
