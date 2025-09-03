{
  "targets": [
    {
      "target_name": "desktop_capture",
      "sources": [
        "native/src/addon.cc",
        "native/src/capturer.cpp"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "native/src"
      ],
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"],
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 0,
          "AdditionalOptions": [ "/EHsc" ]
        }
      },
      "conditions": [
        ["OS=='win'", {
          "libraries": [
            "d3d11.lib",
            "dxgi.lib",
            "dxguid.lib"
          ]
        }]
      ]
    }
  ]
}
