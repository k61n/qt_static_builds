This repo provides Qt libs for static linking that are built in github actions.

A github action from this repo can be used to checkout the release artifact.
This action will download a macOS/Windows x86_64/arm64 release artifact, based
on the system and architecture of the runner that called it. For Windows runners,
please note that it is needed to specify the compiler mingw1310/llvm1706/msvc2022.


      - name: Checkout static Qt release
        id: qtsetup
        uses: k61n/qt_static_builds/.github/actions/checkout@main
        with:
          version: 6.10.0
          # optional for Windows runners
          compiler: <mingw1310|llvm1706|msvc2022>

The action will provide a `qtpath` variable containg the path to directory with
unpacked release artifact, you can use it like this:

    -DCMAKE_PREFIX_PATH=${{steps.qtsetup.outputs.qtpath}}