package demo1

import (
	"fmt"

	"gonum.org/v1/gonum/graph"
	"gonum.org/v1/gonum/graph/simple"
)

func create_simple_undirected_graph() *simple.UndirectedGraph {
	G := simple.NewUndirectedGraph()

	osobe := []simple.Node{
		simple.Node(0),
		simple.Node(1),
		simple.Node(2),
		simple.Node(3),
		simple.Node(4),
	}

	veze := []simple.Edge{
		simple.Edge{F: osobe[0], T: osobe[1]},
		simple.Edge{F: osobe[0], T: osobe[2]},
		simple.Edge{F: osobe[4], T: osobe[3]},
		simple.Edge{F: osobe[2], T: osobe[4]},
		simple.Edge{F: osobe[0], T: osobe[4]},
	}

	for _, osoba := range osobe {
		G.AddNode(osoba)
	}

	for _, veza := range veze {
		G.SetEdge(veza)
	}

	return G
}

func Demo1() {
	G := create_simple_undirected_graph()

	fmt.Println("Osobe:")
	fmt.Println()

	osobe := graph.NodesOf(G.Nodes())

	for _, osoba := range osobe {
		fmt.Println("Osoba", osoba.ID())
	}

	fmt.Println("Veze:")
	fmt.Println()

	veze := graph.EdgesOf(G.Edges())

	for _, veza := range veze {
		fmt.Println(veza.From().ID(), "->", veza.To().ID())
	}

	fmt.Println("\nSusedstva:")

	for _, osoba := range osobe {
		fmt.Println("Susedstva osobe", osoba.ID())

		susedi := graph.NodesOf(G.From(osoba.ID()))

		for _, sused := range susedi {
			fmt.Println(sused.ID())
		}
	}
}
