# Experimental data

Original measurement files are preserved without alteration in `raw/`. Original EPS plots are in `plots/`. Filenames identify the experiment and controller; readable PNG versions of the result plots appear in the main README.

| Experiment | Controller | Raw log | EPS plot | Original filename stem |
|---|---|---|---|---|
| Recovery after maximum deflection | P | [Log](raw/recovery-p.txt) | [Plot](plots/recovery-p.eps) | `PmaxWychylenie` |
| Recovery after maximum deflection | PI | [Log](raw/recovery-pi.txt) | [Plot](plots/recovery-pi.eps) | `PImaxWychylenie` |
| Ground-effect disturbance | P | [Log](raw/ground-effect-p.txt) | [Plot](plots/ground-effect-p.eps) | `Pground` |
| Ground-effect disturbance | PI | [Log](raw/ground-effect-pi.txt) | [Plot](plots/ground-effect-pi.eps) | `PIground` |
| Reference-angle changes | P | [Log](raw/reference-angle-p.txt) | [Plot](plots/reference-angle-p.eps) | `Pkatzadany` |
| Reference-angle changes | PI | [Log](raw/reference-angle-pi.txt) | [Plot](plots/reference-angle-pi.eps) | `PIkatzadany` |

## Log format

The logger records measurement rows in the following format:

```text
LOG,ms,roll,target,kp,ki,kd,pwm1,pwm2
```

- `ms`: milliseconds since ESP32 startup.
- `roll`: filtered measured beam angle in degrees.
- `target`: reference angle in degrees.
- `kp`, `ki`, `kd`: controller gains used at the time of recording.
- `pwm1`, `pwm2`: commands sent to the ESCs, in microseconds.

Files can contain blank lines and session headers such as `--- START LOG ... ---`. Parse measurement rows beginning with `LOG,` rather than assuming every line is numeric data. For a time axis, use the recorded timestamps: the firmware does not use a fixed sampling period.

EPS files may need an external viewer. The complete thesis is not included in this repository.
