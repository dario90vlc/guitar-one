cmake_minimum_required(VERSION 3.22)
project(GuitarAmp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE - framework open source para plugins de audio
add_subdirectory(JUCE)

juce_add_plugin(GuitarAmp
    COMPANY_NAME "OpenAmp"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Oamp
    PLUGIN_CODE Gamp
    FORMATS VST3 Standalone
    PRODUCT_NAME "Guitar Amp"
    VST3_CATEGORIES Fx
    VST3_SUBCATEGORIES "Fx Distortion"
)

target_sources(GuitarAmp PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/AmpModel.cpp
    src/dsp/CleanAmp.cpp
    src/dsp/CrunchAmp.cpp
    src/dsp/DistortionAmp.cpp
    src/dsp/TubeStage.cpp
    src/dsp/ToneStack.cpp
    src/dsp/CabinetSimulator.cpp
    src/dsp/NoiseGate.cpp
    src/gui/AmpKnob.cpp
    src/gui/AmpPanel.cpp
    src/gui/ControlPanel.cpp
)

target_compile_definitions(GuitarAmp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(GuitarAmp PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
