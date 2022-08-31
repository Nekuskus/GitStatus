{
  "targets": [
    {
      "target_name": "getgit",
      "sources": [ "src/addon.cc" ],
      "msvs_settings": {
            "VCCLCompilerTool": {
                "ExceptionHandling": 1
            }
      },
      "conditions": [
        ["OS=='win'", {
          "defines": [
            "_HAS_EXCEPTIONS=1"
          ]
        }]
      ]
    }

  ]
}