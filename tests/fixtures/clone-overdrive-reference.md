# Clone Overdrive audio reference

Generated with `mrgeneko/NeuralAmpModelerCore` commit
`419bb5712e651fb7fb548f8aa54431207727aa59`, using
`nam::wavenet::create_parametric_config_generic` (the Eigen implementation,
not the A2 fast kernels adapted in Ardor), `NAM_SAMPLE_FLOAT`, 48 kHz, 64-frame
buffers, `ResetAndPrewarm`, Gain/Tone both 0.5. Both original release tiers
are covered. Model SHA-256:
`61bb3f25d07920d0fbe8711542df2e585c21ab20d77332816a0de0d66ea148bb`.

4096 input frames: `0.1f*sin(i*0.27f) + 0.02f*sin(i*1.2f)` using C++ float
arguments. Output is sampled every 63 frames. No level normalization or input
calibration is applied; the release loudness is already Ardor's -18 dB target.
The test accepts 2e-5 absolute error to cover compiler/Eigen rounding differences.
