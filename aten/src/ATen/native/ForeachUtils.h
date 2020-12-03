#pragma once
#include <ATen/ATen.h>

namespace at {
namespace native {
namespace {

void check_foreach_api_restrictions(TensorList tensors) {
  TORCH_CHECK(tensors.size() > 0, "Tensor list must have at least one tensor.");
}

void check_foreach_api_restrictions(TensorList tensors, ArrayRef<Scalar> scalars) {
  check_foreach_api_restrictions(tensors);
  TORCH_CHECK(tensors.size() == scalars.size(), "Tensor list must have same number of elements as scalar list.");
}

void check_foreach_api_restrictions(TensorList tensors1, TensorList tensors2) {
  TORCH_CHECK(tensors1.size() > 0, "Tensor list must have at least one tensor.");
  TORCH_CHECK(tensors2.size() > 0, "Tensor list must have at least one tensor.");
  TORCH_CHECK(tensors1.size() == tensors2.size(), "Tensor lists must have the same number of tensors, got ", tensors1.size(), " and ", tensors2.size());

  for (int i = 0; i < tensors1.size(); i++) {
    TORCH_CHECK(tensors1[i].sizes() == tensors2[i].sizes(), "Corresponding tensors in lists must have the same size, got ", tensors1[i].sizes(), " and ", tensors2[i].sizes());
  }
}

void check_foreach_api_restrictions(TensorList tensors1, TensorList tensors2, TensorList tensors3) {
  TORCH_CHECK(tensors1.size() > 0, "Tensor list must have at least one tensor.");
  TORCH_CHECK(tensors2.size() > 0, "Tensor list must have at least one tensor.");
  TORCH_CHECK(tensors3.size() > 0, "Tensor list must have at least one tensor.");
  TORCH_CHECK(tensors1.size() == tensors2.size(), "Tensor lists must have the same number of tensors, got ", tensors1.size(), " and ", tensors2.size());
  TORCH_CHECK(tensors1.size() == tensors3.size(), "Tensor lists must have the same number of tensors, got ", tensors1.size(), " and ", tensors3.size());

  for (int i = 0; i < tensors1.size(); i++) {
    TORCH_CHECK(tensors1[i].sizes() == tensors2[i].sizes(), "Corresponding tensors in lists must have the same size, got ", tensors1[i].sizes(), " and ", tensors2[i].sizes());
    TORCH_CHECK(tensors1[i].sizes() == tensors3[i].sizes(), "Corresponding tensors in lists must have the same size, got ", tensors1[i].sizes(), " and ", tensors3[i].sizes());
  }
}

void check_foreach_api_restrictions(TensorList tensors1, TensorList tensors2, TensorList tensors3, ArrayRef<Scalar> scalars) {
  check_foreach_api_restrictions(tensors1, tensors2, tensors3);
  TORCH_CHECK(tensors1.size() == scalars.size(), "Tensor list must have same number of elements as scalar list, got ", tensors1.size(), " and ", scalars.size());
}

// To go via 'fast' path, several conditions must be satisfied
// - All tensors must be on the same device
// - All tensors must have strided layout
// - All tensors must be non-overlapping and dense
// - Resulting tensor must have the same dtype as the input one

// Check if all tensors have the same device, layout, strides and are not overlapping and dense
bool has_same_attributes(Device expected_device, caffe2::TypeMeta expected_dtype, TensorList tensors) {
  auto expected_strides = tensors[0].strides();
  for (const auto& t : tensors) {
    if (t.dtype() != expected_dtype) {
      return false;
    }

    if (t.device() != expected_device) {
      return false;
    }

    if (t.layout() != at::kStrided) {
      return false;
    }

    if (!t.is_non_overlapping_and_dense()) {
      return false;
    }

    if (t.strides() != expected_strides) {
      return false;
    }
  }

  return true;
}

bool will_promote_tensor(const Tensor& tensor, Scalar scalar, bool division_op = false) {
  // complex scalar + integral or boolean tensor will result in complex tensor
  if (scalar.isComplex() && at::isIntegralType(tensor.scalar_type(), /*includeBool*/ true)) {
    return true;
  }

  // complex scalar + float tensor will result in complex tensor
  if (scalar.isComplex() && at::isFloatingType(tensor.scalar_type())) {
    return true;
  }

  // float scalar + integral or boolean tensor will result in float tensor
  if (scalar.isFloatingPoint() && at::isIntegralType(tensor.scalar_type(), /*includeBool*/ true)) {
    return true;
  }

  // integral scalar + boolean tensor will result in integral tensor
  if (scalar.isIntegral(/*includeBool*/ false) && tensor.dtype() == at::kBool) {
    return true;
  }

  // In case of division, integer inputs will result in float
  if (division_op) {
    if (at::isIntegralType(tensor.scalar_type(), /*includeBool*/ true)) {
      return true;
    }
  }
  return false;
}

bool can_use_fast_route(TensorList tensors) {
#ifdef __HIP_PLATFORM_HCC__
  return false;
#else
  auto expected_device = tensors[0].device();
  auto expected_dtype = tensors[0].dtype();

  for (auto t : tensors) {
    if (!has_same_attributes(expected_device, expected_dtype, {t})) {
      return false;
    }
  }

  return true;
#endif
}

bool can_use_fast_route(TensorList tensors, Scalar scalar, bool division_op = false) {
#ifdef __HIP_PLATFORM_HCC__
  return false;
#else
  auto expected_device = tensors[0].device();
  auto expected_dtype = tensors[0].dtype();

  for (auto t : tensors) {
    if (!has_same_attributes(expected_device, expected_dtype, {t})) {
      return false;
    }

    if (will_promote_tensor(t, scalar, division_op)) {
      return false;
    }
  }

  return true;
#endif
}

bool can_use_fast_route(TensorList tensors, ArrayRef<Scalar> scalars, bool division_op = false) {
#ifdef __HIP_PLATFORM_HCC__
  return false;
#else
  for (int i = 0; i < tensors.size(); i++) {
    if (will_promote_tensor(tensors[i], scalars[i], division_op)) {
      return false;
    }

    // Complex scalar list is not supported.
    if (scalars[i].isComplex() || at::isComplexType(tensors[i].scalar_type())) {
      return false;
    }
  }

  return true;
#endif
}

bool can_use_fast_route(TensorList tensors1, TensorList tensors2, bool division_op = false) {
#ifdef __HIP_PLATFORM_HCC__
  return false;
#else
  auto expected_device = tensors1[0].device();
  auto expected_dtype = tensors1[0].dtype();

  for (int64_t i = 0; i < tensors1.size(); i++) {
    if (!has_same_attributes(expected_device, expected_dtype, {tensors1[i], tensors2[i]})) {
      return false;
    }

    // In case of division, integer inputs will result in float
    if (division_op) {
      if (at::isIntegralType(tensors1[i].scalar_type(), /*includeBool*/ true)) {
        return false;
      }
    }
  }

  return true;
#endif
}

bool can_use_fast_route(TensorList tensors1, TensorList tensors2, Scalar scalar) {
#ifdef __HIP_PLATFORM_HCC__
  return false;
#else
  auto expected_device = tensors1[0].device();
  auto expected_dtype = tensors1[0].dtype();

  for (int64_t i = 0; i < tensors1.size(); i++) {
    if (!has_same_attributes(expected_device, expected_dtype, {tensors1[i], tensors2[i]})) {
      return false;
    }

    if (will_promote_tensor(tensors1[i], scalar)) {
      return false;
    }
  }

  return true;
#endif
}

bool can_use_fast_route(TensorList tensors1, TensorList tensors2, TensorList tensors3) {
#ifdef __HIP_PLATFORM_HCC__
  return false;
#else
  auto expected_device = tensors1[0].device();
  auto expected_dtype = tensors1[0].dtype();

  for (int64_t i = 0; i < tensors1.size(); i++) {
    if (!has_same_attributes(expected_device, expected_dtype, {tensors1[i], tensors2[i], tensors3[i]})) {
      return false;
    }
  }

  return true;
#endif
}

bool can_use_fast_route(TensorList tensors1, TensorList tensors2, TensorList tensors3, Scalar scalar) {
#ifdef __HIP_PLATFORM_HCC__
  return false;
#else
  auto expected_device = tensors1[0].device();
  auto expected_dtype = tensors1[0].dtype();

  for (int64_t i = 0; i < tensors1.size(); i++) {
    if (!has_same_attributes(expected_device, expected_dtype, {tensors1[i], tensors2[i], tensors3[i]})) {
      return false;
    }

    if (will_promote_tensor(tensors1[i], scalar)) {
      return false;
    }
  }

  return true;
#endif
}

bool can_use_fast_route(TensorList tensors1, TensorList tensors2, TensorList tensors3, ArrayRef<Scalar> scalars) {
  return can_use_fast_route(tensors1, tensors2, tensors3);
}

}
}} // at::native
