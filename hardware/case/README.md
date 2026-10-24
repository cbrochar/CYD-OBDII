# 3D-Printed Case

STL files for the CYD-OBDII enclosure. GitHub shows a 3D preview when you click an STL file.

## Credit and license

This case is not my design. It is the **CYD (Cheap Yellow Display) Modular Case** by
**DanielBrunod**, a remix of the original **CYD enclosure** by **ncmreynolds**:

- Modular case by DanielBrunod: <https://www.thingiverse.com/thing:6991968>
- Original enclosure by ncmreynolds: <https://www.thingiverse.com/thing:6308391>

From DanielBrunod's description, the remix:

- **Simplifies the top and bottom layers.** They're smaller and less complex, so they print
  faster and more easily.
- **Fits CYD boards better.** The original groove was too tight for some boards, and this
  version loosens it for a more reliable fit.
- **Makes the case modular.** You stack layers to suit your project and add or remove sections
  as needed.

The STL files in this folder are shared under the same license as the original,
**Creative Commons Attribution-ShareAlike (CC BY-SA)**. Any changes you share must keep
that license and credit both creators.

## Parts

| File | Part | Notes |
|---|---|---|
| [CYDEnclosureFront.stl](CYDEnclosureFront.stl) | Front case | Slimmer, simplified front with reduced wall height and openings for both MicroUSB and USB-C ports |
| [CYDEnclosureRear.stl](CYDEnclosureRear.stl) | Rear case | Has an extra hole for easy access to the reset and reboot buttons with the pen |
| [CYDProtoLayer.stl](CYDProtoLayer.stl) | Proto layer | 1.5 mm layer with a grid of 3 mm holes for mounting add-on boards (GPS, NRF24, etc.) |
| [CYDProtoLayerSpeakerHole.stl](CYDProtoLayerSpeakerHole.stl) | Proto layer, speaker opening | 1 mm layer with a grid of 3 mm holes and an opening for the speaker connector; mounts next to the front case |
| [CYDEnclosureExtender15mm.stl](CYDEnclosureExtender15mm.stl) | 15 mm extender | Intermediate layer with two SMA connector holes |
| [CYDEnclosureExtender10mm.stl](CYDEnclosureExtender10mm.stl) | 10 mm extender | Plain layer with no holes, for adding height or customising |
| [CYDEnclosureExtender6mm.stl](CYDEnclosureExtender6mm.stl) | 6 mm extender | Plain layer with no holes, for adding height or customising (named `CYDEnclosureExtender06mm.stl` in the original listing) |
| [CYD_PenHolder06mm.stl](CYD_PenHolder06mm.stl) | Pen holder layer | 6 mm plain layer with a holder for the pen that comes with the CYD |

The layers stack between the front case and the rear case, so you pick the ones you need for
the depth and features you want.

## Print settings

The original listing doesn't give print settings.

- Material:
- Layer height:
- Infill:
- Supports:

## Assembly

- All exterior holes are sized for **M3 screws**. Screw length depends on how many layers you
  stack, so measure your stack before buying screws.

### This build

- **Parts used:** the standard case only, [CYDEnclosureFront.stl](CYDEnclosureFront.stl) and
  [CYDEnclosureRear.stl](CYDEnclosureRear.stl), with no extender or proto layers.
- **Mounting:** held in place in the truck with double-sided tape.
