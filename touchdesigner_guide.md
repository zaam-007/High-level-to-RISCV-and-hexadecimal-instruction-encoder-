# TouchDesigner Setup Guide for Digital Mirror

This guide outlines the network architecture in TouchDesigner to process the webcam feed and biosignals.

## 1. Input Section
### Webcam & Masking
1.  **Video Device In CHOP**: Select your webcam.
2.  **Threshold TOP**: Adjust `Threshold` so the presenter is white and the background is black.
3.  **Blur TOP**: Add a small amount of blur to the threshold to soften the edges.
4.  **Edge TOP**: (Optional) Use this to create a glowing outline.
5.  **Null TOP (named `MASK`)**: This will be used to mask the generative art.

### OSC Data (Biosignals)
1.  **OSC In CHOP**: Set port to `7000` (matching `osc_bridge.py`).
2.  **Select CHOP**: Grab `/sensor/hrv`, `/sensor/gsr`, and `/sensor/bpm`.
3.  **Math CHOP**: Use this to normalize the values (e.g., HRV 20-100 -> 0-1 range).
4.  **Lag CHOP**: Add a small lag (e.g., 0.2s) to smooth out the sensor jitter.

## 2. Visual Generation
### Background Particles/Noise
1.  **Noise TOP**: Create a dynamic texture.
    - Set `RGB` to a base color.
    - Animate `Transform > Translate Z` with `absTime.seconds * 0.1`.
2.  **Feedback TOP**: Create a trail effect for the visuals.
3.  **Composite TOP**: Combine multiple noise layers.

## 3. Interaction Logic (Python Script in TD or CHOP logic)
Map the signals to parameters:
- **HRV (Stress Indicator)**:
    - High HRV (Calm) -> Set Noise color to **Blue/Cyan**.
    - Low HRV (Stress) -> Set Noise color to **Red**.
    - Use a **Limit CHOP** or **Interpolate** to transition colors.
- **GSR (Excitement/Arousal)**:
    - Map GSR to Noise `Period` or `Amplitude`. Higher GSR = faster, more chaotic motion.

## 4. Final Composition
1.  **Inset Top**: Use your **Generative Art TOP** as the source.
2.  **Mask TOP**: Use the **`MASK` Null TOP** from the webcam section.
3.  **Composite TOP**: Set `Operation` to **Inside**.
4.  **Out TOP**: Connect to a **Window COMP** to display full screen on the projector.

## 5. Cyberpunk Neural Overlay Config (2D Emotion Matrix)
By combining HRV (Valence/Stress) and GSR (Arousal/Intensity), you can create 4 distinct visual states. Use an **Expression CHOP** to evaluate these combinations and drive the visual colors and fluid dynamics.

*Assuming thresholds: HRV High > 60, HRV Low < 40. GSR High > 500, GSR Low < 300.*

| Condition (Sensor Combo) | Mental State | Neon Hex | RGB Value | Visual Effect & Fluid Dynamics |
| :--- | :--- | :--- | :--- | :--- |
| **High HRV + Low GSR** | **Deep Zen** (Calm) | `#7700EE` | `119, 0, 238` | Electric Violet. Slow, smooth, cool fluid motion. |
| **Low HRV + High GSR** | **Critical Stress** | `#FF006E` | `255, 0, 110` | Hot Pink. Fast, erratic, glitchy fluid dispersion. |
| **High HRV + High GSR** | **Flow State** (Excitement) | `#00F0FF` | `0, 240, 255` | Cyan. Fast but smooth, vibrant glowing waves. |
| **Low HRV + Low GSR** | **Burnout / Fatigue** | `#00AACC` | `0, 170, 204` | Dim Teal. Slow, stagnant, muddy, low-opacity trails. |

### How to set this up in TouchDesigner:
1. **Expression CHOP**: Create an `Expression CHOP`. You will create 4 channels inside it (e.g., `zen`, `stress`, `flow`, `burnout`). 
2. Write expressions referencing your normalized sensor data. For example, for the `zen` channel: 
   `1 if op('math_hrv')[0] > 0.6 and op('math_gsr')[0] < 0.3 else 0`
3. **Filter CHOP**: Connect the Expression CHOP to a `Filter CHOP`. This will smoothly interpolate the values (so the visuals crossfade smoothly between states rather than instantly snapping).
4. **Color Mapping**: Use these 4 smoothed 0-1 values (which act like opacities) to drive a `Cross TOP` or `Switch TOP` that blends between 4 different colored `Constant TOPs`. Connect the final color into your Fluid Feedback loop.

### Design Tips for Cyberpunk Vibes:
1.  **Bloom/Glow**: Use a **Blur TOP** and a **Composite TOP (Add)** to create a neon "glow" around your silhouettes.
2.  **Scanlines**: Multiply a fine horizontal grid texture over the final image for a retro-future CRT look.
3.  **Glitch Logic**: When HRV is below 30, trigger a **Displace TOP** using random noise to create "neural glitches."

## 6. Upgrading Your System — Detailed Step-by-Step

---

### Part A: Background Removal (Difference Matte)

This replaces the basic `Threshold TOP` which wrongly picks up furniture,
walls, and objects. The Difference Matte technique only shows what is
*different* from an empty reference frame — i.e., you.

**Step 1 — Set up your live video branch**
1. Press `Tab` to open the OP Create dialog. Type `Video Device In` and press Enter.
2. In its parameters, set `Device` to your webcam.
3. Rename it `webcam_live` (right-click → Rename).

**Step 2 — Capture the empty background frame**
1. Ask everyone to step out of frame completely.
2. Press `Tab` → type `Cache` → Enter. Connect `webcam_live` → `Cache1`.
3. In the `Cache TOP` parameters, click the **Capture** button once.
   You will see the empty room freeze in the thumbnail.
4. Set `Active` parameter to **Off**. The frame is now permanently frozen.
5. Rename it `bg_reference`.

**Step 3 — Compute the Difference**
1. Press `Tab` → type `Difference` → Enter.
2. Connect **Input 1** = `webcam_live`, **Input 2** = `bg_reference`.
3. The thumbnail should now show only the areas that differ from the
   background — this is your raw silhouette (white blobs on black).

**Step 4 — Threshold to get a clean mask**
1. Press `Tab` → type `Threshold` → Enter. Connect `Difference1` → `Threshold1`.
2. Set the `Threshold` slider to around `0.05–0.10`.
   - Too low → background noise bleeds in.
   - Too high → you disappear. Find the sweet spot where only your body is white.
3. Rename this node `threshold_matte`.

**Step 5 — Soften the edges**
1. Press `Tab` → type `Blur` → Enter. Connect `threshold_matte` → `Blur1`.
2. Set `Size` to `3–5 pixels`. This feathers the hard silhouette edge so
   compositing looks natural instead of jagged.

**Step 6 — Dilate to fill gaps**
1. Press `Tab` → type `Dilate` → Enter. Connect `Blur1` → `Dilate1`.
2. Set `Size` to `2`. This slightly expands the mask inward so thin body
   parts (fingers, arms) don't drop out.

**Step 7 — Create the MASK output**
1. Press `Tab` → type `Null` → Enter. Connect `Dilate1` → `Null1`.
2. Rename it `MASK`. This is the clean silhouette mask that feeds into the
   Composite TOP in your Final Composition step.

> **Tip**: If you re-capture the background (e.g., lights change), just
> switch `bg_reference`'s Active back to **On** briefly, click Capture,
> then turn Active **Off** again.

---

### Part B: Fluid Dynamics — Optical Flow Visual Loop

This replaces the static noise texture with a swirling, interactive fluid
that reacts to your body movement. Your motion physically pushes and
displaces the coloured light, like smoke or ink in water.

**Step 1 — Generate Optical Flow vectors**
1. Press `Tab` → type `Optical Flow` → Enter.
2. Connect `webcam_live` → `Optical Flow1`.
3. In its parameters:
   - Set `Smoothness` to `5`.
   - Set `Flow Scale` to `1.0`.
4. Look at the thumbnail — moving your hand should produce a coloured
   vector field (red = right, green = left, blue/yellow = up/down).
5. Rename this node `motion_vectors`.

**Step 2 — Scale down the flow to a gentle displacement**
1. Press `Tab` → type `Math` → Enter. Connect `motion_vectors` → `Math1`.
2. In the Math TOP parameters, set `Multiply` to `0.04`.
   This prevents the flow from displacing the image too violently.
3. Rename it `flow_scaled`.

**Step 3 — Create the Feedback seed**
1. Press `Tab` → type `Feedback` → Enter.
2. Leave it unconnected for now — you will close the loop in Step 7.
3. Set its resolution to `1280 x 720` (or match your output resolution).
4. Rename it `fluid_feedback`.

**Step 4 — Displace the feedback with motion vectors**
1. Press `Tab` → type `Displace` → Enter.
2. Connect **Input 1** = `fluid_feedback`, **Input 2** = `flow_scaled`.
3. In Displace parameters:
   - `Displace U` = `0.04`
   - `Displace V` = `0.04`
   - `Extend U/V` = **Repeat**
4. Rename it `fluid_displaced`.

**Step 5 — Fade trails over time (Persistence)**
1. Press `Tab` → type `Level` → Enter. Connect `fluid_displaced` → `Level1`.
2. Set `Opacity` to `0.92` (8% fade per frame).
   - Higher value = longer, denser trails.
   - Lower value = trails evaporate quickly.
3. Rename it `fluid_faded`.

**Step 6 — Inject biofeedback colour**
1. Press `Tab` → type `Constant` → Enter four times.
   Create four `Constant TOPs` named:
   - `col_zen`     → set colour to `#7700EE` (Electric Violet)
   - `col_stress`  → set colour to `#FF006E` (Hot Pink)
   - `col_flow`    → set colour to `#00F0FF` (Cyan)
   - `col_burnout` → set colour to `#00AACC` (Dim Teal)
2. Press `Tab` → type `Cross` → Enter.
3. Connect `col_zen` → Input 0, `col_stress` → Input 1.
4. Connect your `/state/zen` channel from the OSC In CHOP (via a Select CHOP)
   to the `Cross` parameter field `Blend`. Repeat for all 4 states — you can
   chain Cross TOPs for each state pair.
5. The final colour output from the last Cross → rename it `state_colour`.

**Step 7 — Composite colour into the fluid**
1. Press `Tab` → type `Composite` → Enter. Connect:
   - **Input 0** = `fluid_faded`
   - **Input 1** = `state_colour`
2. Set `Operation` to **Add**.
3. Set `Pre-Multiply Alpha` = **Off**.
4. Rename it `fluid_composite`.

**Step 8 — Close the Feedback Loop**
1. Click on the `fluid_feedback` node.
2. In its parameters, find the `Target TOP` field.
3. **Drag `fluid_composite` into the Target TOP field.**
4. Click the **Reset** button on `fluid_feedback` once.
5. The loop is now active — the fluid is self-sustaining and displacing itself
   every frame based on your movement.

**Step 9 — Mask to Silhouette**
1. Press `Tab` → type `Composite` → Enter.
2. Connect **Input 0** = `fluid_composite`, **Input 1** = `MASK`.
3. Set `Operation` to **Inside** (or **Multiply**).
4. The fluid will now only appear inside your body silhouette.
5. Rename it `final_masked`.

**Step 10 — Add Glow for Aesthetic Appeal**
1. Press `Tab` → type `Blur` → Enter. Connect `final_masked` → `Blur1`.
2. Set `Filter` = **Gaussian**, `Size` = `8`.
3. Press `Tab` → type `Composite` → Enter.
   Connect **Input 0** = `final_masked`, **Input 1** = `Blur1`.
   Set `Operation` = **Add**.
4. This creates the neon bloom glow — the single most impactful step for
   making it look beautiful rather than scientific.
5. Rename it `final_glow`.

**Step 11 — Output to Projector**
1. Press `Tab` → type `Out` → Enter. Connect `final_glow` → `Out1`.
2. Press `Tab` → type `Window` → Enter.
3. In the Window COMP, set `Operator` to `Out1`.
4. Set your projector as the output display and click **Open as Perform**.

---

### Part C: Handle the Idle State (No Finger on Sensor)
When the Python script sends `/sensor/contact = 0`, you should show a
neutral idle animation instead of freezing on the last user's colour.

1. Create a `Noise TOP` with slow animation (`Translate Z = absTime.seconds * 0.02`).
   Set its colour to a dark neutral grey (`#1A1A2E`).
2. Press `Tab` → type `Switch` → Enter.
   Connect **Input 0** = `final_glow` (active state), **Input 1** = the idle `Noise TOP`.
3. Use a **Select CHOP** to grab `/sensor/contact` from the OSC In CHOP.
4. Connect the Select CHOP output to the Switch TOP's `Index` parameter.
   When `contact = 0`, it shows the idle view. When `contact = 1`, it
   shows your fluid visuals.
5. Connect the Switch TOP → `Out1`.

---














## 7. Migrating Your Current TD Setup (Based on Your Existing Network)

> This section is written specifically for your current network visible in your screenshot.
> It tells you exactly what to keep, what to cut, and what to add — node by node.

---

### What You Already Have (Keep These)

| Your Node | Role | Status |
| :--- | :--- | :--- |
| `videodevin1` | Live webcam feed | ✅ Keep |
| `transform2` | Flip/resize webcam | ✅ Keep |
| `thresh1` | Silhouette threshold | ⚠️ Will be upgraded |
| `blur1` | Edge softening | ✅ Keep (move later in chain) |
| `level1` | Brightness/opacity | ✅ Keep (will become trail fade) |
| `oscin1` | OSC receiver on port 7000 | ✅ Keep |
| `hrv` Select CHOP | Grabs HRV channel | ✅ Keep |
| `gsr` Select CHOP | Grabs GSR channel | ✅ Keep |
| `math1`, `math2` | Normalise HRV / GSR | ✅ Keep |
| `constant1` | Color source | ⚠️ Will be replaced with 4 colours |
| `comp1` | Composite | ⚠️ Will be rewired |
| `out1`, `window1` | Output | ✅ Keep |

---

### Step 1 — Update Your OSC In CHOP for New Channels

Your `oscin1` already receives `/sensor/hrv` and `/sensor/gsr`. The new `osc.py`
sends additional channels you need to tap into.

1. Click on `oscin1`. It should already be listening on port `7000` — leave that.
2. Add **4 new Select CHOPs** (press `Tab` → `Select` → Enter each time).
   Wire all 4 from `oscin1` and configure each:
   - `select_zen`     → Channel Name: `/state/zen`
   - `select_stress`  → Channel Name: `/state/stress`
   - `select_flow`    → Channel Name: `/state/flow`
   - `select_burnout` → Channel Name: `/state/burnout`
3. Add one more Select CHOP:
   - `select_contact`     → Channel Name: `/sensor/contact`
   - `select_calibrating` → Channel Name: `/sensor/calibrating`

---

### Step 2 — Replace `constant1` with 4 State Colours

Right now you have a single green `constant1`. This needs to become 4 coloured
sources that crossfade based on the committed state.

1. **Delete** `constant1` (or repurpose it).
2. Add 4 new `Constant TOPs`:
   - `col_zen`     → R:`0.47` G:`0.0`  B:`0.93`  (Electric Violet `#7700EE`)
   - `col_stress`  → R:`1.0`  G:`0.0`  B:`0.43`  (Hot Pink `#FF006E`)
   - `col_flow`    → R:`0.0`  G:`0.94` B:`1.0`   (Cyan `#00F0FF`)
   - `col_burnout` → R:`0.0`  G:`0.67` B:`0.80`  (Dim Teal `#00AACC`)
3. Add a **Filter CHOP** for each of your 4 Select CHOPs (zen/stress/flow/burnout).
   Set `Filter Width` to `1.0 second`. This makes the colour crossfade smoothly
   instead of snapping.
4. Now chain **3 Cross TOPs** to blend between the 4 colours:
   - `cross1`: Input 0 = `col_zen`, Input 1 = `col_stress`.
     Wire `filter_stress` output → `cross1` `Blend` parameter.
   - `cross2`: Input 0 = `cross1`, Input 1 = `col_flow`.
     Wire `filter_flow` → `cross2` `Blend`.
   - `cross3`: Input 0 = `cross2`, Input 1 = `col_burnout`.
     Wire `filter_burnout` → `cross3` `Blend`.
5. `cross3` is now your **active state colour**. Rename it `state_colour`.

---

### Step 3 — Insert Difference Matte Before `thresh1`

Your current chain is: `videodevin1` → `transform2` → `mono1` → `thresh1`.
Insert the Difference Matte between `transform2` and `mono1`.

1. **Step everyone out of frame.**
2. Add a `Cache TOP`. Connect `transform2` → `Cache1`.
3. In `Cache1` parameters → click **Capture** once → set `Active` to **Off**.
   Rename it `bg_reference`.
4. Add a `Difference TOP`.
   - Input 1 = output of `transform2` (live)
   - Input 2 = `bg_reference` (frozen)
5. **Disconnect** the wire from `transform2` → `mono1`.
   Replace it: connect `Difference1` → `mono1`.
6. Your chain is now:
   `transform2` → `Difference1` → `mono1` → `thresh1` → `blur1`

> If the difference matte looks too noisy, increase `thresh1`'s threshold
> slider slightly until only your body remains white.

---

### Step 4 — Build the Optical Flow Fluid Loop

This is a new parallel branch — it does NOT replace your existing silhouette
chain. It runs alongside it and is masked by the silhouette at the end.

1. Add an `Optical Flow TOP`. Connect `transform2` → `Optical Flow1`.
   Set `Smoothness` = `5`, `Flow Scale` = `1.0`. Rename: `motion_vectors`.
2. Add a `Math TOP`. Connect `motion_vectors` → `Math1`.
   Set `Multiply` = `0.04`. Rename: `flow_scaled`.
3. Add a `Feedback TOP`. Set resolution to `1280×720`. Rename: `fluid_feedback`.
   *(Leave Target TOP blank for now — you'll close the loop in step 7.)*
4. Add a `Displace TOP`.
   - Input 1 = `fluid_feedback`
   - Input 2 = `flow_scaled`
   - `Displace U` = `0.04`, `Displace V` = `0.04`, Extend = `Repeat`
   Rename: `fluid_displaced`.
5. **Your existing `level1`** — disconnect it from wherever it currently is.
   Connect `fluid_displaced` → `level1`. Set `Opacity` = `0.92`.
   Rename: `fluid_faded`. *(level1 now serves as the trail fade.)*
6. Add a `Composite TOP`.
   - Input 0 = `fluid_faded`
   - Input 1 = `state_colour` (from Step 2)
   - Operation = **Add**
   Rename: `fluid_composite`.
7. **Close the loop**: Click `fluid_feedback` → drag `fluid_composite` into
   the `Target TOP` parameter field → click **Reset**.

---

### Step 5 — Rewire `comp1` to Mask Fluid to Silhouette

Your existing `comp1` was compositing the colour over the silhouette. Rewire it
to mask the fluid output inside the silhouette instead.

1. **Disconnect** `comp1`'s current inputs.
2. Reconnect:
   - Input 0 = `fluid_composite` (the swirling fluid)
   - Input 1 = output of `blur1` (your silhouette mask)
   - Operation = **Inside** (or **Multiply**)
3. This makes the fluid only appear inside your body shape.
4. Add a `Blur TOP` after `comp1`. Set `Filter` = Gaussian, `Size` = `8`.
5. Add a new `Composite TOP`:
   - Input 0 = `comp1`
   - Input 1 = the new `Blur TOP`
   - Operation = **Add**
   Rename this: `final_glow`.
6. **Disconnect** `final` (or delete it). Wire `final_glow` → `out1` → `window1`.

---

### Step 6 — Add Idle Switch for `contact = 0`

1. Add a `Noise TOP`. Set colour to dark grey-blue (`R:0.1 G:0.1 B:0.18`).
   Animate it: `Translate Z` parameter → type `absTime.seconds * 0.02`.
   Rename: `idle_bg`.
2. Add a `Switch TOP`.
   - Input 0 = `final_glow` (active visuals)
   - Input 1 = `idle_bg`
3. Connect `select_contact` CHOP → `Switch TOP` `Index` parameter.
   - When `contact = 1` → shows active fluid (`final_glow`)
   - When `contact = 0` → shows idle animation
4. Wire the Switch TOP → `out1`.

---

### Step 7 — Add Calibration Visual Cue (Optional but Recommended)

When `/sensor/calibrating = 1`, you should show a subtle scan animation.

1. Add a `Text TOP`. Type: `"Calibrating...  Hold still."` Set font size small.
   Set text colour to white, background to transparent.
2. Add a `Composite TOP`.
   - Input 0 = the Switch TOP output
   - Input 1 = `Text TOP`
   - Operation = **Over**
3. Drive the Text TOP's `Opacity` from `select_calibrating`:
   Right-click the Opacity parameter → Export → select `select_calibrating`.
   When calibrating = 0, text is invisible. When calibrating = 1, it appears.
4. Wire this final Composite → `out1`.

---

### Final Node Map (After All Changes)

```
videodevin1 → transform2 ─┬─→ bg_reference (frozen Cache)
                           ├─→ Difference1 → mono1 → thresh1 → blur1 ─────────────────────┐
                           └─→ Optical Flow1 → flow_scaled                                  │ (mask)
                                                    │                                       ↓
                               fluid_feedback → fluid_displaced → level1 → fluid_composite → comp1 → final_glow
                                      ↑                                           │
                                      └───────────────────────────────────────────┘ (feedback loop closed)

oscin1 → select_zen/stress/flow/burnout → Filter CHOPs → Cross TOPs → state_colour → fluid_composite (Input 1)
oscin1 → select_contact → Switch TOP Index
oscin1 → select_calibrating → Text TOP Opacity

Switch TOP (Input 0 = final_glow, Input 1 = idle_bg) → Calibration Overlay Composite → out1 → window1
```
