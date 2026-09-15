#ifndef SSAO_CONVERGENCE_GLSL
#define SSAO_CONVERGENCE_GLSL

shared uint ssaoMaxDiff;

// All invocations must call this with the same sample count.
bool ssaoIsSolved(float diff, int samples) {
  if(gl_LocalInvocationIndex==0)
    ssaoMaxDiff = 0;
  barrier();

  atomicMax(ssaoMaxDiff, floatBitsToUint(diff));
  barrier();

  // The previous counter counted the same group-wide comparison in every lane.
  // Keep its unordered/NaN behavior as well as its convergence threshold.
  bool solved = !(uintBitsToFloat(ssaoMaxDiff)>=0.1 + samples*0.005);
  // Every lane must read the result before another iteration can reset it.
  barrier();
  return solved;
  }

#endif
