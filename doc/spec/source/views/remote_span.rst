.. SPDX-FileCopyrightText: Intel Corporation
..
.. SPDX-License-Identifier: BSD-3-Clause

.. include:: ../include/distributed-ranges.rst

.. _remote_span:

=================
 ``remote_span``
=================

Interface
=========

.. doxygenclass:: lib::remote_span
   :members:

Description
===========

``remote_span`` is a generalization of `std::span`_ that refers to a
span of *remote memory*, that is memory that may be located on another
process inside a parallel program.

Like `std::span`_, ``remote_span`` refers to a span of contiguous
memory, however, instead of using regular C++ pointers and references,
it uses objects of type ``remote_ptr`` and ``remote_ref``, which
behave similarly to pointers and references but reference memory that
may be on a remote process.  In addition, the ``rank()`` method
returns the rank on which the ``remote_span`` is located.

.. seealso::

   `std::span`_

Examples
========
