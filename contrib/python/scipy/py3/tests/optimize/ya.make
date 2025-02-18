# Generated by devtools/yamaker.

PY3TEST()

SUBSCRIBER(g:python-contrib)

VERSION(1.11.4)

ORIGINAL_SOURCE(mirror://pypi/s/scipy/scipy-1.11.4.tar.gz)

SIZE(MEDIUM)

FORK_SUBTESTS()

PEERDIR(
    contrib/python/scipy/py3/tests
)

NO_LINT()

SRCDIR(contrib/python/scipy/py3)

TEST_SRCS(
    scipy/optimize/_trustregion_constr/tests/__init__.py
    scipy/optimize/_trustregion_constr/tests/test_canonical_constraint.py
    scipy/optimize/_trustregion_constr/tests/test_projections.py
    scipy/optimize/_trustregion_constr/tests/test_qp_subproblem.py
    scipy/optimize/_trustregion_constr/tests/test_report.py
    scipy/optimize/tests/__init__.py
    scipy/optimize/tests/test__basinhopping.py
    scipy/optimize/tests/test__differential_evolution.py
    scipy/optimize/tests/test__dual_annealing.py
    scipy/optimize/tests/test__linprog_clean_inputs.py
    scipy/optimize/tests/test__numdiff.py
    scipy/optimize/tests/test__remove_redundancy.py
    scipy/optimize/tests/test__root.py
    scipy/optimize/tests/test__shgo.py
    scipy/optimize/tests/test__spectral.py
    scipy/optimize/tests/test_cobyla.py
    scipy/optimize/tests/test_constraint_conversion.py
    scipy/optimize/tests/test_constraints.py
    scipy/optimize/tests/test_cython_optimize.py
    scipy/optimize/tests/test_differentiable_functions.py
    scipy/optimize/tests/test_direct.py
    scipy/optimize/tests/test_hessian_update_strategy.py
    scipy/optimize/tests/test_lbfgsb_hessinv.py
    scipy/optimize/tests/test_lbfgsb_setulb.py
    scipy/optimize/tests/test_least_squares.py
    scipy/optimize/tests/test_linesearch.py
    scipy/optimize/tests/test_linprog.py
    scipy/optimize/tests/test_lsq_common.py
    scipy/optimize/tests/test_lsq_linear.py
    scipy/optimize/tests/test_milp.py
    scipy/optimize/tests/test_minimize_constrained.py
    scipy/optimize/tests/test_minpack.py
    scipy/optimize/tests/test_nnls.py
    scipy/optimize/tests/test_nonlin.py
    scipy/optimize/tests/test_optimize.py
    scipy/optimize/tests/test_quadratic_assignment.py
    scipy/optimize/tests/test_regression.py
    scipy/optimize/tests/test_slsqp.py
    scipy/optimize/tests/test_tnc.py
    scipy/optimize/tests/test_trustregion.py
    scipy/optimize/tests/test_trustregion_exact.py
    scipy/optimize/tests/test_trustregion_krylov.py
    scipy/optimize/tests/test_zeros.py
)

END()
