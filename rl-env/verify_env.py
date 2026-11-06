"""Verify the basic PyTorch and Gymnasium setup inside the course VM."""

import gymnasium as gym
import numpy as np
import scipy
import matplotlib
import torch
import stable_baselines3


print(f"Python packages: PyTorch {torch.__version__}, Gymnasium {gym.__version__}, "
      f"Stable-Baselines3 {stable_baselines3.__version__}")
print(f"NumPy {np.__version__}, SciPy {scipy.__version__}, "
      f"Matplotlib {matplotlib.__version__}")
print(f"CUDA available: {torch.cuda.is_available()}")
print(f"Tensor result: {(torch.tensor([1.0, 2.0]) * 2).tolist()}")

env = gym.make("CartPole-v1", render_mode="human")
try:
    observation, info = env.reset()
    for _ in range(200):
        action = env.action_space.sample()
        observation, reward, terminated, truncated, info = env.step(action)
        if terminated or truncated:
            observation, info = env.reset()
finally:
    env.close()

print("最小环境验证通过！")
