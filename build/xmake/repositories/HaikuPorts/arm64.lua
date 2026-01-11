--[[
    HaikuPorts/arm64.lua - HaikuPorts repository for arm64

    xmake equivalent of build/jam/repositories/HaikuPorts/arm64
]]

import("rules.RepositoryRules")

-- ============================================================================
-- Repository Configuration
-- ============================================================================

ARCHITECTURE = "arm64"
REPOSITORY_URL = "https://eu.hpkg.haiku-os.org/haikuports/master/build-packages"

-- ============================================================================
-- Architecture "any" Packages
-- ============================================================================

ANY_PACKAGES = {
    -- "be_book-2008_10_26-3",  -- commented out in original
    "ca_root_certificates-2022_10_11-1",
    "haikuporter-1.2.7-1",
    "noto-20200106-1",
    "timgmsoundfont-fixed-5",
    "wqy_microhei-0.2.0~beta-4",
}

-- ============================================================================
-- Architecture-specific Packages
-- ============================================================================

ARCH_PACKAGES = {
    "bash-4.4.023-1",
    "binutils-2.41_2023_08_05-1",
    "bison-3.0.5-1",
    "coreutils-8.22-1",
    "curl-7.40.0-1",
    "curl_devel-7.40.0-1",
    "expat-2.5.0-1",
    "expat_devel-2.5.0-1",
    "findutils-4.6.0-1",
    "flex-2.5.35-1",
    "freetype-2.6.3-1",
    "freetype_devel-2.6.3-1",
    "gawk-3.1.8-2",
    "gcc-13.2.0_2023_08_10-1",
    "gcc_syslibs_devel-13.2.0_2023_08_10-1",
    "gcc_syslibs-13.2.0_2023_08_10-1",
    "icu-67.1-2",
    "icu_devel-67.1-2",
    "less-451-1",
    "libsolv-0.3.0_haiku_2014_12_22-1",
    "libsolv_devel-0.3.0_haiku_2014_12_22-1",
    "m4-1.4.16-1",
    "make-4.3-1",
    "ncurses6-6.2-1",
    "ncurses6_devel-6.2-1",
    "python-3.9.1-1",
    "sed-4.2.1-1",
    "texinfo-4.13a-1",
    "zlib-1.2.13-1",
    "zlib_devel-1.2.13-1",
}

-- ============================================================================
-- Source Packages
-- ============================================================================

SOURCE_PACKAGES = {
    "apr", "autoconf", "automake", "bash", "binutils", "bison", "bzip2",
    "cdrtools", "coreutils", "cmake", "ctags", "curl", "cvs", "doxygen",
    "expat", "findutils", "flex", "freetype", "gawk", "gcc", "gettext", "git",
    "glu", "gperf", "grep", "groff", "haikuwebkit", "htmldoc", "icu", "jam",
    "jpeg", "keymapswitcher", "less", "libedit", "libiconv", "libpcre",
    "libpng", "libsolv", "libtool", "libxml2", "m4", "make", "man",
    "mercurial", "mesa", "mkdepend", "nano", "ncurses6", "neon", "openssh",
    "openssl", "p7zip", "pe", "perl", "pkgconfig", "python", "readline",
    "scons", "sed", "sqlite", "subversion", "tar", "texi2html", "texinfo",
    "vision", "wpa_supplicant", "yasm", "xz_utils", "zlib",
    -- commented out: "bepdf", "ffmpeg", "libogg", "libtheora", "libvorbis", "libvpx", "speex"
}

-- ============================================================================
-- Debug Info Packages
-- ============================================================================

DEBUGINFO_PACKAGES = {}

-- ============================================================================
-- Repository Registration
-- ============================================================================

function main()
    local source_lookup = {}
    for _, pkg in ipairs(SOURCE_PACKAGES) do
        source_lookup[pkg] = true
    end

    RepositoryRules.RemotePackageRepository(
        "HaikuPorts",
        ARCHITECTURE,
        REPOSITORY_URL,
        ANY_PACKAGES,
        ARCH_PACKAGES,
        source_lookup,
        {}
    )
end