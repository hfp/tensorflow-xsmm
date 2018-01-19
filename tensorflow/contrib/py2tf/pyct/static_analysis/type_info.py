# Copyright 2017 The TensorFlow Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================
"""Type resolution.

Requires annotations generated by LiveValuesResolver.
"""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import gast

from tensorflow.contrib.py2tf.pyct import anno
from tensorflow.python.util import tf_inspect


class Scope(object):
  """Encloses symbol value references.

  Attributes:
    values: A dict mapping string to gast.Node, containing the value that was
        most recently assigned to the symbol.
  """

  # TODO(mdan): Should rather use a CFG here?

  def __init__(self, parent):
    """Create a new scope.

    Args:
      parent: A Scope or None.
    """
    self.parent = parent
    self.values = {}

  def __repr__(self):
    return 'Scope[%s]' % self.values.keys()

  def copy(self):
    s = Scope(self.parent)
    s.values = self.values.copy()
    return s

  def setval(self, name, value):
    self.values[name] = value

  def hasval(self, name):
    return (name in self.values or
            (self.parent is not None and self.parent.hasval(name)))

  def getval(self, name):
    if name in self.values:
      return self.values[name]
    if self.parent is not None:
      return self.parent.getval(name)
    raise KeyError(name)


class TypeInfoResolver(gast.NodeTransformer):
  """Annotates symbols with type information where possible.

  Nodes currently annotated:
    * Call (helps detect class constructors)
    * Attribute (helps resolve object methods)
  """

  def __init__(self, value_hints):
    self.scope = Scope(None)
    self.value_hints = value_hints
    self.function_level = 0

  def visit_FunctionDef(self, node):
    self.scope = Scope(self.scope)
    self.function_level += 1
    self.generic_visit(node)
    self.function_level -= 1
    self.scope = self.scope.parent
    return node

  def _visit_block(self, block):
    self.scope = Scope(self.scope)
    for i, n in enumerate(block):
      block[i] = self.generic_visit(n)
    self.scope = self.scope.parent
    return block

  def visit_For(self, node):
    self.generic_visit(node.target)
    self.generic_visit(node.iter)
    node.body = self._visit_block(node.body)
    node.orelse = self._visit_block(node.orelse)
    return node

  def visit_While(self, node):
    self.generic_visit(node.test)
    node.body = self._visit_block(node.body)
    node.orelse = self._visit_block(node.orelse)
    return node

  def visit_If(self, node):
    self.generic_visit(node.test)
    node.body = self._visit_block(node.body)
    node.orelse = self._visit_block(node.orelse)
    return node

  def visit_Name(self, node):
    self.generic_visit(node)
    if isinstance(node.ctx, gast.Param):
      self.scope.setval(node.id, gast.Name(node.id, gast.Load(), None))
      # TODO(mdan): Member functions should not need type hints.
      # We could attemp to extract im_class from the live_val annotation.
      if self.function_level == 1 and node.id in self.value_hints:
        # Forge a node to hold the type information, so that method calls on
        # it can resolve the type.
        type_holder = gast.Name(node.id, gast.Load(), None)
        type_string, type_obj = self.value_hints[node.id]
        anno.setanno(type_holder, 'type', type_obj)
        anno.setanno(type_holder, 'type_fqn', tuple(type_string.split('.')))
        self.scope.setval(node.id, type_holder)
    return node

  def _process_variable_assignment(self, source, targets):
    if isinstance(source, gast.Call):
      func = source.func
      if anno.hasanno(func, 'live_val'):
        func_obj = anno.getanno(func, 'live_val')
        if tf_inspect.isclass(func_obj):
          anno.setanno(source, 'is_constructor', True)
          anno.setanno(source, 'type', func_obj)
          anno.setanno(source, 'type_fqn', anno.getanno(func, 'fqn'))
          # TODO(mdan): Raise an error if constructor has side effects.
          # We can have a whitelist of no-side-effects constructors.
          # We can also step inside the constructor and further analyze.

    for t in targets:
      if isinstance(t, gast.Tuple):
        for i, e in enumerate(t.elts):
          self.scope.setval(e.id,
                            gast.Subscript(
                                source, gast.Index(i), ctx=gast.Store()))
      elif isinstance(t, gast.Name):
        self.scope.setval(t.id, source)
      elif isinstance(t, gast.Attribute):
        if not (isinstance(t.value, gast.Name) and t.value.id == 'self'):
          raise ValueError(
              'Dont know how to handle assignment to attributes of objects'
              ' other than "self": [%s].%s' % (t.value, t.attr))
      else:
        raise ValueError('Dont know how to handle assignment to %s' % t)

  def visit_With(self, node):
    for wi in node.items:
      if wi.optional_vars is not None:
        self._process_variable_assignment(wi.context_expr, (wi.optional_vars,))
    self.generic_visit(node)
    return node

  def visit_Assign(self, node):
    self.generic_visit(node)
    self._process_variable_assignment(node.value, node.targets)
    return node

  def visit_Call(self, node):
    target = node.func
    if not anno.hasanno(target, 'live_val'):
      if not isinstance(target, gast.Attribute):
        # Suspecting this pattern would reach here:
        #   foo = bar
        #   foo()
        raise ValueError('Dont know how to handle dynamic functions.')
      if not isinstance(target.value, gast.Name):
        # Possible example of this kind:
        #   foo = module.Foo()
        #   foo.bar.baz()
        # TODO(mdan): This should be doable by using the FQN.
        raise ValueError('Dont know how to handle object properties yet.')
      # In the example below, object_source is 'tr.train.Optimizer()':
      #   opt = tf.train.Optimizer()
      #   opt.foo()
      if self.scope.hasval(target.value.id):
        object_source = self.scope.getval(target.value.id)
        if not anno.hasanno(object_source, 'type'):
          raise ValueError('Could not determine type of "%s". Is it dynamic?' %
                           (target.value.id))
        anno.setanno(target, 'type', anno.getanno(object_source, 'type'))
        anno.setanno(target, 'type_fqn', anno.getanno(object_source,
                                                      'type_fqn'))
      else:
        # TODO(mdan): Figure out what could the user do to get past this.
        raise ValueError('No info on "%s". Is it dynamically built?' %
                         (target.value.id))
    self.generic_visit(node)
    return node


def resolve(node, value_hints):
  assert value_hints is not None
  return TypeInfoResolver(value_hints).visit(node)
