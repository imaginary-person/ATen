#include "gtest/gtest.h"

#include "ATen/ATen.h"
#include <caffe2/core/init.h>
#include <caffe2/core/operator.h>

TEST(Caffe2ToPytorch, SimpleLegacy) {
  caffe2::Tensor c2_tensor(caffe2::CPU);
  c2_tensor.Resize(4, 4);
  auto data = c2_tensor.mutable_data<int64_t>();
  for (int64_t i = 0; i < 16; i++) {
    data[i] = i;
  }
  at::Tensor at_tensor(c2_tensor);
  ASSERT_TRUE(&at_tensor.type() != nullptr);

  auto it = at_tensor.data<int64_t>();
  for (int64_t i = 0; i < 16; i++) {
    ASSERT_EQ(it[i], i);
  }
}

TEST(Caffe2ToPytorch, Simple) {
  caffe2::Tensor c2_tensor = caffe2::empty({4, 4}, at::kLong);
  auto data = c2_tensor.mutable_data<int64_t>();
  for (int64_t i = 0; i < 16; i++) {
    data[i] = i;
  }
  at::Tensor at_tensor(c2_tensor);
  ASSERT_TRUE(&at_tensor.type() != nullptr);

  auto it = at_tensor.data<int64_t>();
  for (int64_t i = 0; i < 16; i++) {
    ASSERT_EQ(it[i], i);
  }
}

TEST(Caffe2ToPytorch, Op) {
  caffe2::Tensor c2_tensor(caffe2::CPU);
  c2_tensor.Resize(3, 3);
  auto data = c2_tensor.mutable_data<int64_t>();
  for (int64_t i = 0; i < 9; i++) {
    data[i] = i;
  }
  at::Tensor at_tensor(c2_tensor);

  ASSERT_EQ(at::sum(at_tensor).item<int64_t>(), 36);
}

// Caffe2 doesn't actually have another always-on backend that is not CPU or GPU
// TEST(Caffe2ToPytorch, UnsupportedDevice) {
//   caffe2::Tensor c2_tensor(caffe2::OPENGL);
//   c2_tensor.Resize(4, 4);
//   c2_tensor.mutable_data<float>();
//   at::Tensor at_tensor(c2_tensor);
//   ASSERT_ANY_THROW(at::sum(at_tensor));
// }

TEST(Caffe2ToPytorch, PartiallyInitialized) {
  // These APIs for partially initialized tensors should go away soon, in the
  // meantime ensure they are caught
  {
    // no dtype, no storage
    caffe2::Tensor c2_tensor(caffe2::CPU);
    ASSERT_ANY_THROW(at::Tensor at_tensor(c2_tensor));
  }
  {
    // storage, no dtype
    caffe2::Tensor c2_tensor(caffe2::CPU);
    c2_tensor.Resize(4,4);
    ASSERT_ANY_THROW(at::Tensor at_tensor(c2_tensor));
  }
  {
    // dtype, no storage
    caffe2::Tensor c2_tensor(caffe2::CPU);
    c2_tensor.Resize(4,4);
    c2_tensor.mutable_data<float>();
    c2_tensor.FreeMemory();
    ASSERT_ANY_THROW(at::Tensor at_tensor(c2_tensor));
  }
}

TEST(PytorchToCaffe2, Op) {
  caffe2::Workspace workspace;
  caffe2::NetDef net;

  auto at_tensor_a = at::ones({5, 5}, at::dtype(at::kFloat));
  auto at_tensor_b = at::ones({5, 5}, at::dtype(at::kFloat));
  auto at_tensor_c = at::ones({5, 5}, at::dtype(at::kFloat));

  auto* c2_tensor_a = BlobSetTensor(workspace.CreateBlob("a"), caffe2::Tensor(at_tensor_a));
  auto* c2_tensor_b = BlobSetTensor(workspace.CreateBlob("b"), caffe2::Tensor(at_tensor_b));

  // Test Alias
  {
    caffe2::Tensor c2_tensor_from_aten(at_tensor_c);
    BlobSetTensor(workspace.CreateBlob("c"), c2_tensor_from_aten.Alias());
  }

  {
    auto op = net.add_op();
    op->set_type("Sum");
    op->add_input("a");
    op->add_input("b");
    op->add_input("c");
    op->add_output("d");
  }

  workspace.RunNetOnce(net);

  auto result = XBlobGetMutableTensor(workspace.CreateBlob("d"), {5, 5}, at::kCPU);

  auto it = result.data<float>();
  for (int64_t i = 0; i < 25; i++) {
    ASSERT_EQ(it[i], 3.0);
  }
  at::Tensor at_result(result);
  ASSERT_EQ(at::sum(at_result).item<float>(), 75);
}

TEST(PytorchToCaffe2, SharedStorageRead) {
  caffe2::Workspace workspace;
  caffe2::NetDef net;

  auto at_tensor_a = at::ones({5, 5}, at::dtype(at::kFloat));
  auto at_tensor_b = at_tensor_a.view({5, 5});

  auto* c2_tensor_a = BlobSetTensor(workspace.CreateBlob("a"), caffe2::Tensor(at_tensor_a));
  auto* c2_tensor_b = BlobSetTensor(workspace.CreateBlob("b"), caffe2::Tensor(at_tensor_b));

  {
    auto op = net.add_op();
    op->set_type("Add");
    op->add_input("a");
    op->add_input("b");
    op->add_output("c");
  }

  workspace.RunNetOnce(net);

  auto result = XBlobGetMutableTensor(workspace.CreateBlob("c"), {5, 5}, at::kCPU);
  auto it = result.data<float>();
  for (int64_t i = 0; i < 25; i++) {
    ASSERT_EQ(it[i], 2.0);
  }
  at::Tensor at_result(result);
  ASSERT_EQ(at::sum(at_result).item<float>(), 50);
}

TEST(PytorchToCaffe2, SharedStorageWrite) {
  auto at_tensor_a = at::ones({5, 5}, at::dtype(at::kFloat));
  auto at_tensor_b = at_tensor_a.view({25});

  caffe2::Tensor c2_tensor_a(at_tensor_a);
  caffe2::Tensor c2_tensor_b(at_tensor_b);

  // change is visible everywhere
  c2_tensor_a.mutable_data<float>()[1] = 123;
  ASSERT_EQ(c2_tensor_b.mutable_data<float>()[1], 123);
  ASSERT_EQ(at_tensor_a[0][1].item().to<float>(), 123);
  ASSERT_EQ(at_tensor_b[1].item().to<float>(), 123);
}

TEST(PytorchToCaffe2, MutualResizes) {
  auto at_tensor = at::ones({5, 5}, at::dtype(at::kFloat));

  caffe2::Tensor c2_tensor(at_tensor);

  // change is visible
  c2_tensor.mutable_data<float>()[0] = 123;
  ASSERT_EQ(at_tensor[0][0].item().to<float>(), 123);

  // resize PT tensor in smaller direction - storage is preserved
  at_tensor.resize_({4, 4});
  c2_tensor.mutable_data<float>()[1] = 234;
  ASSERT_EQ(at_tensor[0][1].item().to<float>(), 234);

  // resize PT tensor in larger direction - storage is preserved
  at_tensor.resize_({6, 6});
  c2_tensor.mutable_data<float>()[2] = 345;
  ASSERT_EQ(at_tensor[0][2].item().to<float>(), 345);
  ASSERT_EQ(c2_tensor.sizes()[0], 6);
  ASSERT_EQ(c2_tensor.sizes()[1], 6);

  // resize Caffe2 tensor - semantics are to NOT preserve the data, but the
  // TensorImpl is still shared
  c2_tensor.Resize(7, 7);
  c2_tensor.mutable_data<float>()[3] = 456;
  ASSERT_EQ(at_tensor[0][3].item().to<float>(), 456);
  ASSERT_EQ(at_tensor.sizes()[0], 7);
  ASSERT_EQ(at_tensor.sizes()[1], 7);
}

TEST(PytorchToCaffe2, Strided) {
  auto at_tensor = at::ones({5, 5}, at::dtype(at::kFloat)).t();
  ASSERT_ANY_THROW(caffe2::Tensor c2_tensor(at_tensor));
  // but calling contiguous is fine
  caffe2::Tensor c2_tensor(at_tensor.contiguous());
  for (int64_t i = 0; i < 25; i++) {
    ASSERT_EQ(c2_tensor.data<float>()[i], 1.0);
  }
}

TEST(PytorchToCaffe2, InplaceStrided) {
  auto at_tensor = at::zeros({2, 5}, at::dtype(at::kFloat));
  caffe2::Tensor c2_tensor(at_tensor);
  ASSERT_EQ(c2_tensor.sizes()[0], 2);
  ASSERT_EQ(c2_tensor.sizes()[1], 5);

  c2_tensor.mutable_data<float>()[1] = 234;
  ASSERT_EQ(at_tensor[0][1].item().to<float>(), 234);

  at_tensor.t_();
  ASSERT_EQ(c2_tensor.sizes()[0], 5);
  ASSERT_EQ(c2_tensor.sizes()[1], 2);
  // This is BROKEN situation, however checking is_contiguous on every data
  // access is expensive. We rely on user to not do crazy stuff.
  ASSERT_EQ(at_tensor[1][0].item().to<float>(), 234);
  ASSERT_EQ(c2_tensor.data<float>()[1], 234);
}

TEST(PytorchToCaffe2, NonRegularTensor) {
  at::Tensor at_tensor =
      at::empty({2, 3}, at::dtype<float>().layout(at::kSparse));
  ASSERT_TRUE(at_tensor.is_sparse());
  ASSERT_ANY_THROW(caffe2::Tensor c2_tensor(at_tensor));
}

// With current build system it's too bothersome to set it up, but the test
// passes
// TEST(PytorchToCaffe2, Variable) {
//   at::Tensor var =
//       torch::autograd::make_variable(at::empty({2, 3}, at::dtype<float>()));
//   ASSERT_TRUE(var.is_variable());
//   ASSERT_ANY_THROW(caffe2::Tensor c2_tensor(var));
// }

TEST(Caffe2ToPytorch, NonPOD) {
  caffe2::Tensor c2_tensor = caffe2::empty({1}, at::dtype<std::string>());
  auto data = c2_tensor.mutable_data<std::string>();
  *data = "test";
  ASSERT_ANY_THROW(at::Tensor at_tensor(c2_tensor));
}

TEST(Caffe2ToPytorch, Nullptr) {
  caffe2::Tensor c2_tensor;
  ASSERT_FALSE(c2_tensor.defined());
  at::Tensor at_tensor(c2_tensor);
  ASSERT_FALSE(at_tensor.defined());
}

TEST(PytorchToCaffe2, Nullptr) {
  at::Tensor at_tensor;
  ASSERT_FALSE(at_tensor.defined());
  caffe2::Tensor c2_tensor(at_tensor);
  ASSERT_FALSE(c2_tensor.defined());
}
