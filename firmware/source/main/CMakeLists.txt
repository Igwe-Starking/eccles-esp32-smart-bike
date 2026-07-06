# main application component: just main.cpp, which depends on the "eccles" component for
# everything else (devices, audio, executors, transport, tts...)

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES eccles
)
