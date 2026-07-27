vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO vlonexti/Kore-Library
    REF "v${VERSION}"
    SHA512 f5f1298764c459013497f80d053702b62542564541928a41a50f3567fbff5ffe22598e7feee5516ab979f564dbac78df671cad3f0dcccce4cd7855f2fa5d2469
    HEAD_REF main
)

vcpkg_check_features(
    OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        opengl KORE_BACKEND_OPENGL
        d3d11  KORE_BACKEND_D3D11
        d3d9   KORE_BACKEND_D3D9
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        # vcpkg supplies imgui and minhook; the in-tree FetchContent path would
        # try to reach the network from inside the build sandbox.
        -DKORE_USE_EXTERNAL_DEPS=ON
        -DKORE_BUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    PACKAGE_NAME KoreLibrary
    CONFIG_PATH share/KoreLibrary
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
