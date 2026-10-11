#!/usr/bin/env python3
"""Train Ardor's MIT-licensed synthetic overdrive demo, not a hardware capture.
Requires torch. CPU only; deterministic seed; export ConcatLSTM 1.0.0.
Target: Drive-controlled tanh soft clip into a Tone-controlled one-pole lowpass.
"""
import argparse
import json
import math
from pathlib import Path
import torch

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--steps', type=int, default=1600)
    parser.add_argument('--output', type=Path, default=Path('assets/nam/Ardor-Parametric-Overdrive-Demo.nam'))
    args = parser.parse_args()
    torch.manual_seed(187)
    torch.set_num_threads(4)
    network = torch.nn.LSTM(3, 12, batch_first=True)
    head = torch.nn.Linear(12, 1)
    optimizer = torch.optim.Adam(list(network.parameters()) + list(head.parameters()), lr=0.003)
    def batch(count, frames):
        drive = torch.rand(count, 1)
        tone = torch.rand(count, 1)
        time = torch.arange(frames)[None, :]
        frequencies = torch.exp(torch.rand(count, 4, 1) * math.log(8000/70)) * 70
        phases = torch.rand(count, 4, 1) * 2 * math.pi
        audio = torch.sin(time[:, None, :] * frequencies * (2*math.pi/48000) + phases).sum(1)
        audio *= torch.rand(count, 1) * 0.2
        audio += torch.randn_like(audio) * 0.015
        clipped = 0.22 * torch.tanh(audio * (1.5 + 28.5 * drive))
        alpha = 1 - torch.exp(-2*math.pi*(700 + 9300 * tone)/48000)
        target = torch.empty_like(audio)
        state = torch.zeros(count)
        for i in range(frames):
            state = state + alpha[:, 0] * (clipped[:, i] - state)
            target[:, i] = state
        controls = torch.stack((drive[:, 0]*2-1, tone[:, 0]*2-1), -1)[:, None, :].expand(-1, frames, -1)
        return torch.cat((audio[..., None], controls), -1), target[..., None]
    for step in range(args.steps):
        inputs, target = batch(24, 192)
        result, _ = network(inputs)
        predicted = head(result)
        loss = ((predicted[:, 24:] - target[:, 24:]) ** 2).mean()
        optimizer.zero_grad()
        loss.backward()
        torch.nn.utils.clip_grad_norm_(list(network.parameters()) + list(head.parameters()), 1)
        optimizer.step()
        if step % 200 == 0:
            print(f'step={step} mse={loss.item():.7f}', flush=True)
    with torch.no_grad():
        inputs, target = batch(48, 1024)
        predicted = head(network(inputs)[0])
        esr = ((predicted[:, 96:] - target[:, 96:])**2).sum() / (target[:, 96:]**2).sum()
    # NAM reads IFGO rows over concatenated [input, hidden], then summed biases,
    # initial hidden/cell state, linear head weights and bias.
    weights = torch.cat((network.weight_ih_l0, network.weight_hh_l0), dim=1).detach().flatten().tolist()
    weights += (network.bias_ih_l0 + network.bias_hh_l0).detach().tolist()
    weights += [0.0] * 24
    weights += head.weight.detach().flatten().tolist() + head.bias.detach().tolist()
    document = {'version':'1.0.0', 'architecture':'ConcatLSTM', 'sample_rate':48000,
        'metadata':{'name':'Ardor Parametric Overdrive Demo', 'gear_type':'pedal',
            'license':'MIT', 'license_text':(Path(__file__).resolve().parents[1] / 'assets/nam/LICENSE').read_text(),
            'author':'Ardor contributors',
            'description':'Synthetic soft-clipping and lowpass demonstration; not a hardware capture or ParametricOD.',
            'validation_esr':round(esr.item(),6)},
        'config':{'hidden_size':12, 'num_layers':1, 'params':[
            {'name':'Drive','min':0,'max':10,'default':5,'type':'continuous'},
            {'name':'Tone','min':0,'max':10,'default':5,'type':'continuous'}]}, 'weights':weights}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, separators=(',', ':'))+'\n')
    print(f'exported {args.output}: {len(weights)} weights, validation ESR={esr.item():.6f}', flush=True)
if __name__ == '__main__':
    main()
