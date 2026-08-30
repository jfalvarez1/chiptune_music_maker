# Sample Import - Integration TODO

## Completed:
✅ Created `Sample.h` with Sample struct, SamplePool, and SampleOscillator
✅ Added `sampleID` field to Note struct (Types.h:189)
✅ Added `sampleID` and `SampleOscillator` to Voice struct (Synthesizer.h:55-56)
✅ Included Sample.h in Synthesizer.h

## Remaining Work:

### 1. Synthesizer Integration (Synthesizer.h)
**File:** `src/Synthesizer.h`

**In `Synthesizer` class:**
- Add `SamplePool* m_samplePool` member variable
- Add constructor parameter to pass SamplePool pointer
- Update `triggerNote()` to handle samples:
  ```cpp
  void triggerNote(int note, float velocity, const Note* noteData, const SamplePool* samplePool) {
      // ... existing code ...

      voice.sampleID = noteData ? noteData->sampleID : -1;
      if (voice.sampleID >= 0 && samplePool) {
          const Sample* sample = samplePool->getSample(voice.sampleID);
          if (sample) {
              voice.sampleOscillator.trigger(sample, note, velocity);
          }
      }
  }
  ```

**In `renderVoice()` or `process()` function:**
- Check if `voice.sampleID >= 0`
- If so, use `voice.sampleOscillator.process()` instead of synthesized oscillator
  ```cpp
  float renderVoice(Voice& voice, float deltaTime) {
      if (voice.sampleID >= 0) {
          return voice.sampleOscillator.process();
      }

      // ... existing synth code ...
  }
  ```

### 2. UI Integration (UI.h)
**File:** `src/UI.h`

**Add Sample Browser Window:**
```cpp
inline void DrawSampleBrowser(SamplePool& samplePool, UIState& ui) {
    if (!ImGui::Begin("Sample Browser")) {
        ImGui::End();
        return;
    }

    // Import button
    if (ImGui::Button("Import Sample (WAV/MP3/OGG)")) {
        std::string path = openFileDialog(
            "Audio Files\0*.wav;*.mp3;*.ogg\0",
            "wav");
        if (!path.empty()) {
            int id = samplePool.loadSample(path);
            if (id >= 0) {
                // Success
            }
        }
    }

    ImGui::Separator();

    // Display loaded samples
    const auto& samples = samplePool.getAllSamples();
    for (size_t i = 0; i < samples.size(); i++) {
        const Sample& sample = samples[i];

        ImGui::PushID((int)i);

        if (ImGui::Selectable(sample.name.c_str(), ui.selectedSampleID == (int)i)) {
            ui.selectedSampleID = (int)i;
        }

        // Show sample info
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Duration: %.2fs\nRoot: MIDI %d\nLoop: %s",
                sample.lengthSeconds,
                sample.rootNote,
                sample.loop ? "Yes" : "No");
        }

        // Right-click menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Set as current sound")) {
                // Set to use this sample for new notes
            }
            if (ImGui::MenuItem("Delete")) {
                samplePool.removeSample((int)i);
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    ImGui::End();
}
```

**Add to UIState struct:**
```cpp
int selectedSampleID = -1;
bool showSampleBrowser = false;
```

**Add menu item:**
```cpp
if (ImGui::MenuItem("Sample Browser")) {
    ui.showSampleBrowser = !ui.showSampleBrowser;
}
```

**In main UI loop:**
```cpp
if (ui.showSampleBrowser) {
    DrawSampleBrowser(project.samplePool, ui);
}
```

### 3. Project Integration (Types.h)
**File:** `src/Types.h`

**Add to Project struct:**
```cpp
// In Project struct (around line 396)
SamplePool samplePool;
```

**This requires:**
- Forward declare or include Sample.h in Types.h
- OR keep SamplePool separate and pass as parameter (current approach)

### 4. File I/O Integration (FileIO.h)
**File:** `src/FileIO.h`

**Save samples with project:**
```cpp
// In saveProject():
// Save sample references (not full audio data - too large!)
file << "SAMPLES " << samplePool.getCount() << "\n";
for (int i = 0; i < samplePool.getCount(); i++) {
    const Sample* sample = samplePool.getSample(i);
    file << "SAMPLE " << sample->filepath << " "
         << sample->rootNote << " "
         << (sample->loop ? 1 : 0) << "\n";
}
```

**Load samples:**
```cpp
// In loadProject():
else if (token == "SAMPLES") {
    int count;
    ss >> count;
    for (int i = 0; i < count; i++) {
        std::string filepath;
        int rootNote;
        bool loop;
        // Parse and load...
        samplePool.loadSample(filepath);
    }
}
```

### 5. Main.cpp Integration
**File:** `src/main.cpp`

**Add global SamplePool:**
```cpp
static SamplePool g_samplePool;
```

**Pass to Synthesizer constructor:**
```cpp
Synthesizer synth(&g_samplePool);
```

**Pass to UI functions:**
```cpp
DrawUI(project, ui, seq, g_samplePool);
```

## Testing Plan:
1. Import 808 kick.wav sample
2. Select sample in browser
3. Place note on piano roll
4. Verify sample plays at correct pitch
5. Test pitch-shifting (C4 = original pitch, C5 = +12 semitones)
6. Test looping samples
7. Test saving/loading project with samples

## Notes:
- Samples are NOT saved in .ctp files (too large!)
- Only file paths are saved
- Samples must be in same location when loading project
- Future: Add "Pack Samples" feature to embed small samples (<1MB) in project file
